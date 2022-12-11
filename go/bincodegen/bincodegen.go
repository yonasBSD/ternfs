package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"reflect"
	"regexp"
	"strings"
	"unicode"
	"xtx/eggsfs/msgs"
)

type subexpr struct {
	typ reflect.Type
	tag string
	fld string
}

func assertExpectedTag(e *subexpr, expected string) {
	if e.tag != "" {
		panic(fmt.Sprintf("expected %s for subexpr %s", expected, e.fld))
	}
}

func assertNoTag(e *subexpr) {
	assertExpectedTag(e, "no tag")
}

type goCodegen struct {
	pack   *bytes.Buffer
	unpack *bytes.Buffer
	tabs   []byte
	fresh  *int
}

// pack line
func (cg *goCodegen) pline(e string) {
	fmt.Fprintf(cg.pack, "%s%s\n", cg.tabs, e)
}

// unpack line
func (cg *goCodegen) uline(e string) {
	fmt.Fprintf(cg.unpack, "%s%s\n", cg.tabs, e)
}

// unpack step, with error checking
func (cg *goCodegen) ustep(e string) {
	fmt.Fprintf(cg.unpack, "%sif err := %s; err != nil {\n", cg.tabs, e)
	fmt.Fprintf(cg.unpack, "%s\treturn err\n", cg.tabs)
	fmt.Fprintf(cg.unpack, "%s}\n", cg.tabs)
}

// lack of pointer is intentional: we don't want to destructively
// update the tabs.
func (cg goCodegen) genInSlice(expr *subexpr) {
	cg.tabs = cg.tabs[:len(cg.tabs)+1]
	cg.gen(expr)
}

func (cg *goCodegen) lenVar() string {
	*cg.fresh = *cg.fresh + 1
	return fmt.Sprintf("len%d", *cg.fresh)
}

func sliceTypeElem(t reflect.Type) reflect.Type {
	if t.Kind() == reflect.String {
		return reflect.TypeOf(uint8(0))
	}
	return t.Elem()
}

func (cg *goCodegen) gen(expr *subexpr) {
	t := expr.typ
	switch t.Kind() {
	case reflect.Uint8:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU8(uint8(%v))", expr.fld))
		cg.ustep(fmt.Sprintf("buf.UnpackU8((*uint8)(&%s))", expr.fld))
	case reflect.Bool:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackBool(bool(%v))", expr.fld))
		cg.ustep(fmt.Sprintf("buf.UnpackBool((*bool)(&%s))", expr.fld))
	case reflect.Uint16:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU16(uint16(%v))", expr.fld))
		cg.ustep(fmt.Sprintf("buf.UnpackU16((*uint16)(&%s))", expr.fld))
	case reflect.Uint32:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU32(uint32(%v))", expr.fld))
		cg.ustep(fmt.Sprintf("buf.UnpackU32((*uint32)(&%s))", expr.fld))
	case reflect.Uint64:
		if expr.tag == "varint" {
			cg.pline(fmt.Sprintf("buf.PackVarU61(uint64(%v))", expr.fld))
			cg.ustep(fmt.Sprintf("buf.UnpackVarU61((*uint64)(&%s))", expr.fld))
		} else {
			assertExpectedTag(expr, `tag bincode:"varint" or no tag`)
			cg.pline(fmt.Sprintf("buf.PackU64(uint64(%v))", expr.fld))
			cg.ustep(fmt.Sprintf("buf.UnpackU64((*uint64)(&%s))", expr.fld))
		}
	case reflect.Struct:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("%v.Pack(buf)", expr.fld))
		cg.ustep(fmt.Sprintf("%v.Unpack(buf)", expr.fld))
	case reflect.Slice, reflect.String:
		elem := sliceTypeElem(expr.typ)
		if elem.Kind() == reflect.Uint8 {
			cg.pline(fmt.Sprintf("buf.PackBytes([]byte(%v))", expr.fld))
			if expr.typ.Kind() == reflect.String {
				cg.ustep(fmt.Sprintf("buf.UnpackString(&%v)", expr.fld))
			} else {
				cg.ustep(fmt.Sprintf("buf.UnpackBytes((*[]byte)(&%v))", expr.fld))
			}
		} else {
			lenVar := cg.lenVar()
			// handle length
			cg.pline(fmt.Sprintf("%s := len(%s)", lenVar, expr.fld))
			cg.pline(fmt.Sprintf("buf.PackLength(%s)", lenVar))
			cg.uline(fmt.Sprintf("var %s int", lenVar))
			cg.ustep(fmt.Sprintf("buf.UnpackLength(&%s)", lenVar))
			// handle body
			cg.uline(fmt.Sprintf("bincode.EnsureLength(&%s, %s)", expr.fld, lenVar))
			loop := fmt.Sprintf("for i := 0; i < %s; i++ {", lenVar)
			cg.pline(loop)
			cg.uline(loop)
			cg.genInSlice(&subexpr{
				typ: elem,
				fld: fmt.Sprintf("%s[i]", expr.fld),
			})
			cg.pline("}")
			cg.uline("}")
		}
	case reflect.Array:
		elem := sliceTypeElem(expr.typ)
		if elem.Kind() != reflect.Uint8 {
			panic(fmt.Sprintf("we only support arrays of bytes, got %v", elem.Kind()))
		}
		len := expr.typ.Size()
		cg.pline(fmt.Sprintf("buf.PackFixedBytes(%d, %v[:])", len, expr.fld))
		cg.ustep(fmt.Sprintf("buf.UnpackFixedBytes(%d, %v[:])", len, expr.fld))
	default:
		panic(fmt.Sprintf("unsupported type %v with kind %v", expr.typ, expr.typ.Kind()))
	}
}

func parseTag(tag reflect.StructTag) string {
	if string(tag) == "" {
		return ""
	}
	r := regexp.MustCompile(`^bincode:"([a-z0-9]+)"$`)
	body := r.FindStringSubmatch(string(tag))
	if body == nil {
		panic(fmt.Sprintf(`invalid tag %v, expecting something of the form bincode:"command"`, tag))
	}
	return body[1]
}

func generateGoSingle(out io.Writer, t reflect.Type) {
	if t.Kind() != reflect.Struct {
		panic(fmt.Sprintf("type %v is not a struct", t))
	}

	tabs := make([]byte, 100)
	for i := 0; i < len(tabs); i++ {
		tabs[i] = '\t'
	}
	fresh := 0
	cg := goCodegen{
		tabs:   tabs[:0],
		pack:   new(bytes.Buffer),
		unpack: new(bytes.Buffer),
		fresh:  &fresh,
	}

	cg.pline(fmt.Sprintf("func (v *%s) Pack(buf *bincode.Buf) {", t.Name()))
	cg.uline(fmt.Sprintf("func (v *%s) Unpack(buf *bincode.Buf) error {", t.Name()))
	for i := 0; i < t.NumField(); i++ {
		fld := t.Field(i)
		cg.genInSlice(&subexpr{
			fld: fmt.Sprintf("v.%s", fld.Name),
			tag: parseTag(fld.Tag),
			typ: fld.Type,
		})
	}
	cg.uline("\treturn nil")
	cg.pline("}\n")
	cg.uline("}\n")

	out.Write(cg.pack.Bytes())
	out.Write(cg.unpack.Bytes())
}

func enumName(t reflect.Type) string {
	tName := t.Name()
	if !strings.HasSuffix(tName, "Req") && !strings.HasSuffix(tName, "Resp") && !strings.HasSuffix(tName, "Entry") {
		panic(fmt.Errorf("bad req/resp name %v", tName))
	}
	trimmed := strings.TrimSuffix(strings.TrimSuffix(strings.TrimSuffix(tName, "Req"), "Resp"), "Entry")
	re := regexp.MustCompile(`(.)([A-Z])`)
	return strings.ToUpper(string(re.ReplaceAll([]byte(trimmed), []byte("${1}_${2}"))))
}

func reqRespEnum(rr reqRespType) string {
	reqEnum := enumName(rr.req)
	respEnum := enumName(rr.resp)
	if reqEnum != respEnum {
		panic(fmt.Errorf("bad req/resp pair: %v and %v", rr.req, rr.resp))
	}
	return reqEnum
}

func generateGoMsgKind(out io.Writer, typeName string, funName string, reqResps []reqRespType) {
	fmt.Fprintf(out, "func %s(body any) %s {\n", funName, typeName)
	fmt.Fprintf(out, "\tswitch body.(type) {\n")
	fmt.Fprintf(out, "\tcase ErrCode:\n")
	fmt.Fprintf(out, "\t\treturn 0\n")
	seenKinds := map[uint8]bool{}
	for _, reqResp := range reqResps {
		present := seenKinds[reqResp.kind]
		if present {
			panic(fmt.Errorf("duplicate kind %d for %s", reqResp.kind, typeName))
		}
		seenKinds[reqResp.kind] = true
		reqName := reqResp.req.Name()
		respName := reqResp.resp.Name()
		kindName := reqRespEnum(reqResp)
		fmt.Fprintf(out, "\tcase *%v, *%v:\n", reqName, respName)
		fmt.Fprintf(out, "\t\treturn %s\n", kindName)
	}
	fmt.Fprintf(out, "\tdefault:\n")
	fmt.Fprintf(out, "\t\tpanic(fmt.Sprintf(\"bad shard req/resp body %%T\", body))\n")
	fmt.Fprintf(out, "\t}\n")
	fmt.Fprintf(out, "}\n\n")

	fmt.Fprintf(out, "\n")

	fmt.Fprintf(out, "const (\n")
	for _, reqResp := range reqResps {
		fmt.Fprintf(out, "\t%s %s = 0x%X\n", reqRespEnum(reqResp), typeName, reqResp.kind)
	}
	fmt.Fprintf(out, ")\n\n")
}

type reqRespType struct {
	kind uint8
	req  reflect.Type
	resp reflect.Type
}

// Start from 10 to play nice with kernel drivers and such.
const errCodeOffset = 10

func generateGoErrorCodes(out io.Writer, errors []string) {
	fmt.Fprintf(out, "const (\n")
	for i, err := range errors {
		fmt.Fprintf(out, "\t%s ErrCode = %d\n", err, i+errCodeOffset)
	}
	fmt.Fprintf(out, ")\n")
	fmt.Fprintf(out, "\n")

	fmt.Fprintf(out, "func (err ErrCode) String() string {\n")
	fmt.Fprintf(out, "\tswitch err {\n")
	for i, err := range errors {
		fmt.Fprintf(out, "\tcase %d:\n", i+errCodeOffset)
		fmt.Fprintf(out, "\t\treturn \"%s\"\n", err)
	}
	fmt.Fprintf(out, "\tdefault:\n")
	fmt.Fprintf(out, "\t\treturn fmt.Sprintf(\"ErrCode(%%d)\", err)\n")
	fmt.Fprintf(out, "\t}\n")
	fmt.Fprintf(out, "}\n\n")
}

func generateGo(errors []string, shardReqResps []reqRespType, cdcReqResps []reqRespType, extras []reflect.Type) []byte {
	out := new(bytes.Buffer)

	fmt.Fprintln(out, "// Automatically generated with go run bincodegen.")
	fmt.Fprintln(out, "// Run `go generate ./...` from the go/ directory to regenerate it.")
	fmt.Fprintln(out, `package msgs`)
	fmt.Fprintln(out)

	fmt.Fprintln(out, `import "fmt"`)
	fmt.Fprintln(out, `import "xtx/eggsfs/bincode"`)
	fmt.Fprintln(out)

	generateGoErrorCodes(out, errors)

	generateGoMsgKind(out, "ShardMessageKind", "GetShardMessageKind", shardReqResps)
	generateGoMsgKind(out, "CDCMessageKind", "GetCDCMessageKind", cdcReqResps)

	for _, reqResp := range shardReqResps {
		generateGoSingle(out, reqResp.req)
		generateGoSingle(out, reqResp.resp)
	}
	for _, reqResp := range cdcReqResps {
		generateGoSingle(out, reqResp.req)
		generateGoSingle(out, reqResp.resp)
	}
	for _, typ := range extras {
		generateGoSingle(out, typ)
	}

	return out.Bytes()
}

type pythonCodegen struct {
	pack           *bytes.Buffer
	unpack         *bytes.Buffer
	staticSize     []string
	staticSizeInfo []string
	inSlice        bool
	spaces         []byte
	size           *bytes.Buffer
}

func pythonType(t reflect.Type) string {
	if t.Name() == "InodeType" {
		return "InodeType"
	}
	if t.Name() == "InodeIdExtra" {
		return "InodeIdWithExtra"
	}
	switch t.Kind() {
	case reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		return "int"
	case reflect.Bool:
		return "bool"
	case reflect.Struct:
		return t.Name()
	case reflect.Slice, reflect.String, reflect.Array:
		elem := sliceTypeElem(t)
		if elem.Kind() == reflect.Uint8 {
			return "bytes"
		} else {
			return fmt.Sprintf("List[%s]", pythonType(elem))
		}
	default:
		panic(fmt.Sprintf("unsupported type with kind %v", t.Kind()))
	}
}

func upperCamelToSnake(s string) string {
	re := regexp.MustCompile(`(.)([A-Z])`)
	return strings.ToLower(string(re.ReplaceAll([]byte(s), []byte("${1}_${2}"))))
}

func (gc *pythonCodegen) pline(s string) {
	gc.pack.Write([]byte(fmt.Sprintf("%s%s\n", gc.spaces, s)))
}

func (gc *pythonCodegen) uline(s string) {
	gc.unpack.Write([]byte(fmt.Sprintf("%s%s\n", gc.spaces, s)))
}

func (gc *pythonCodegen) sadd(what string, s string) {
	gc.size.Write([]byte(fmt.Sprintf("%s_size += %s # %s\n", gc.spaces, s, what)))
}

func (gc *pythonCodegen) sline(s string) {
	gc.size.Write([]byte(fmt.Sprintf("%s%s\n", gc.spaces, s)))
}

func (gc *pythonCodegen) addStaticSize(what string, s string, sameAsSize bool) {
	if !gc.inSlice {
		gc.staticSize = append(gc.staticSize, s)
		gc.staticSizeInfo = append(gc.staticSizeInfo, what)
	}
	if sameAsSize {
		gc.sadd(what, s)
	}
}

// lack of pointer is intentional: we don't want to destructively
// update the tabs.
func (cg pythonCodegen) genInSlice(expr *subexpr) {
	cg.spaces = cg.spaces[:len(cg.spaces)+4]
	cg.inSlice = true
	cg.gen(expr)
}

func (cg *pythonCodegen) gen(expr *subexpr) {
	if expr.typ.Name() == "InodeType" {
		cg.addStaticSize(expr.fld, "1", true)
		cg.pline(fmt.Sprintf("bincode.pack_u8_into(self.%s, b)", expr.fld))
		cg.uline(fmt.Sprintf("%s = InodeType(bincode.unpack_u8(u))", expr.fld))
		return
	}
	if expr.typ.Name() == "InodeIdExtra" {
		cg.addStaticSize(expr.fld, "8", true)
		cg.pline(fmt.Sprintf("bincode.pack_u64_into(self.%s, b)", expr.fld))
		cg.uline(fmt.Sprintf("%s = InodeIdWithExtra(bincode.unpack_u64(u))", expr.fld))
		return
	}
	switch expr.typ.Kind() {
	case reflect.Uint8:
		cg.addStaticSize(expr.fld, "1", true)
		cg.pline(fmt.Sprintf("bincode.pack_u8_into(self.%s, b)", expr.fld))
		cg.uline(fmt.Sprintf("%s = bincode.unpack_u8(u)", expr.fld))
	case reflect.Bool:
		cg.addStaticSize(expr.fld, "1", true)
		cg.pline(fmt.Sprintf("bincode.pack_u8_into(self.%s, b)", expr.fld))
		cg.uline(fmt.Sprintf("%s = bool(bincode.unpack_u8(u))", expr.fld))
	case reflect.Uint16:
		cg.addStaticSize(expr.fld, "2", true)
		cg.pline(fmt.Sprintf("bincode.pack_u16_into(self.%s, b)", expr.fld))
		cg.uline(fmt.Sprintf("%s = bincode.unpack_u16(u)", expr.fld))
	case reflect.Uint32:
		cg.addStaticSize(expr.fld, "4", true)
		cg.pline(fmt.Sprintf("bincode.pack_u32_into(self.%s, b)", expr.fld))
		cg.uline(fmt.Sprintf("%s = bincode.unpack_u32(u)", expr.fld))
	case reflect.Uint64:
		if expr.tag == "varint" {
			cg.pline(fmt.Sprintf("bincode.pack_v61_into(self.%s, b)", expr.fld))
			cg.uline(fmt.Sprintf("%s = bincode.unpack_v61(u)", expr.fld))
			cg.sadd(expr.fld, fmt.Sprintf("bincode.v61_packed_size(self.%s)", expr.fld))
		} else {
			cg.addStaticSize(expr.fld, "8", true)
			cg.pline(fmt.Sprintf("bincode.pack_u64_into(self.%s, b)", expr.fld))
			cg.uline(fmt.Sprintf("%s = bincode.unpack_u64(u)", expr.fld))
		}
	case reflect.Struct:
		cg.addStaticSize(expr.fld, fmt.Sprintf("%s.STATIC_SIZE", expr.typ.Name()), false)
		cg.sadd(expr.fld, fmt.Sprintf("self.%s.calc_packed_size()", expr.fld))
		cg.pline(fmt.Sprintf("self.%s.pack_into(b)", expr.fld))
		cg.uline(fmt.Sprintf("%s = %s.unpack(u)", expr.fld, expr.typ.Name()))
	case reflect.Slice, reflect.String:
		elem := sliceTypeElem(expr.typ)
		if elem.Kind() == reflect.Uint8 {
			cg.addStaticSize(fmt.Sprintf("len(%s)", expr.fld), "1", true) // u8 len
			cg.sadd(fmt.Sprintf("%s contents", expr.fld), fmt.Sprintf("len(self.%s)", expr.fld))
			cg.pline(fmt.Sprintf("bincode.pack_bytes_into(self.%s, b)", expr.fld))
			cg.uline(fmt.Sprintf("%s = bincode.unpack_bytes(u)", expr.fld))
		} else {
			cg.addStaticSize(fmt.Sprintf("len(%s)", expr.fld), "2", true) // u16 len
			// pack
			cg.pline(fmt.Sprintf("bincode.pack_u16_into(len(self.%s), b)", expr.fld))
			cg.pline(fmt.Sprintf("for i in range(len(self.%s)):", expr.fld))
			// unpack
			cg.uline(fmt.Sprintf("%s: List[Any] = [None]*bincode.unpack_u16(u)", expr.fld))
			cg.uline(fmt.Sprintf("for i in range(len(%s)):", expr.fld))
			// size
			cg.sline(fmt.Sprintf("for i in range(len(self.%s)):", expr.fld))
			cg.genInSlice(&subexpr{
				typ: elem,
				fld: fmt.Sprintf("%s[i]", expr.fld),
			})
		}
	case reflect.Array:
		elem := sliceTypeElem(expr.typ)
		if elem.Kind() != reflect.Uint8 {
			panic(fmt.Sprintf("we only support arrays of bytes, got %v", elem.Kind()))
		}
		len := expr.typ.Size()
		cg.addStaticSize(expr.fld, fmt.Sprintf("%d", len), true)
		cg.pline(fmt.Sprintf("bincode.pack_fixed_into(self.%s, %d, b)", expr.fld, len))
		cg.uline(fmt.Sprintf("%s = bincode.unpack_fixed(u, %d)", expr.fld, len))
	default:
		panic(fmt.Sprintf("unsupported type with kind %v", expr.typ.Kind()))
	}
}

func generatePythonSingle(w io.Writer, t reflect.Type, which string) {
	if t.Kind() != reflect.Struct {
		panic(fmt.Sprintf("type %v is not a struct", t))
	}

	spaces := make([]byte, 100)
	for i := 0; i < len(spaces); i++ {
		spaces[i] = ' '
	}
	cg := pythonCodegen{
		pack:       new(bytes.Buffer),
		unpack:     new(bytes.Buffer),
		spaces:     spaces[:8],
		staticSize: make([]string, 0),
		size:       new(bytes.Buffer),
	}

	names := make([]string, t.NumField())
	for i := 0; i < t.NumField(); i++ {
		fld := t.Field(i)
		name := upperCamelToSnake(fld.Name)
		names[i] = name
		cg.gen(&subexpr{
			typ: fld.Type,
			tag: parseTag(fld.Tag),
			fld: name,
		})
	}

	fmt.Fprintf(w, "@dataclass\n")
	fmt.Fprintf(w, "class %s(bincode.Packable):\n", t.Name())
	if which != "" {
		fmt.Fprintf(w, "    KIND: ClassVar[%sMessageKind] = %sMessageKind.%s\n", which, which, enumName(t))
	}
	if len(cg.staticSize) == 0 {
		cg.staticSize = append(cg.staticSize, "0")
	}
	fmt.Fprintf(w, "    STATIC_SIZE: ClassVar[int] = %s # %s\n", strings.Join(cg.staticSize, " + "), strings.Join(cg.staticSizeInfo, " + "))
	for i := 0; i < t.NumField(); i++ {
		fld := t.Field(i)
		fmt.Fprintf(w, "    %s: %s\n", upperCamelToSnake(fld.Name), pythonType(fld.Type))
	}
	fmt.Fprintf(w, "\n")
	fmt.Fprintf(w, "    def pack_into(self, b: bytearray) -> None:\n")
	w.Write(cg.pack.Bytes())
	fmt.Fprintf(w, "        return None\n") // in case the body is empty
	fmt.Fprintf(w, "\n")
	fmt.Fprintf(w, "    @staticmethod\n")
	fmt.Fprintf(w, "    def unpack(u: bincode.UnpackWrapper) -> '%s':\n", t.Name())
	w.Write(cg.unpack.Bytes())
	fmt.Fprintf(w, "        return %s(%s)\n", t.Name(), strings.Join(names, ", "))
	fmt.Fprintf(w, "\n")
	fmt.Fprintf(w, "    def calc_packed_size(self) -> int:\n")
	fmt.Fprintf(w, "        _size = 0\n")
	w.Write(cg.size.Bytes())
	fmt.Fprintf(w, "        return _size\n")
	fmt.Fprintf(w, "\n")
}

func generatePythonErrorCodes(w io.Writer, errors []string) {
	fmt.Fprintf(w, "class ErrCode(enum.IntEnum):\n")
	for i, err := range errors {
		fmt.Fprintf(w, "    %s = %d\n", err, i+errCodeOffset)
	}
	fmt.Fprintf(w, "\n")
}

func generatePythonMsgKind(out io.Writer, which string, reqResps []reqRespType) {
	fmt.Fprintf(out, "class %sMessageKind(enum.IntEnum):\n", which)
	for _, reqResp := range reqResps {
		fmt.Fprintf(out, "    %s = 0x%X\n", reqRespEnum(reqResp), reqResp.kind)
	}
	fmt.Fprintf(out, "\n")
}

func generatePythonReqRespUnion(out io.Writer, which string, reqResps []reqRespType) {
	// generate union types
	reqTypes := make([]string, len(reqResps))
	respTypes := make([]string, len(reqResps))
	for i, reqResp := range reqResps {
		reqTypes[i] = reqResp.req.Name()
		respTypes[i] = reqResp.resp.Name()
	}
	fmt.Fprintf(out, "%sRequestBody = Union[%s]\n", which, strings.Join(reqTypes, ", "))
	fmt.Fprintf(out, "%sResponseBody = Union[%s]\n", which, strings.Join(respTypes, ", "))
	fmt.Fprintf(out, "\n")
}

func generatePythonKindToReqResp(out io.Writer, which string, reqResps []reqRespType) {
	// generate mapping from kinds to types
	fmt.Fprintf(out, "%s_REQUESTS: Dict[%sMessageKind, Tuple[Type[%sRequestBody], Type[%sResponseBody]]] = {\n", strings.ToUpper(which), which, which, which)
	for _, reqResp := range reqResps {
		fmt.Fprintf(out, "    %sMessageKind.%s: (%s, %s),\n", which, reqRespEnum(reqResp), reqResp.req.Name(), reqResp.resp.Name())
	}
	fmt.Fprintf(out, "}\n")
	fmt.Fprintf(out, "\n")
}

func generatePython(errors []string, shardReqResps []reqRespType, cdcReqResps []reqRespType, extras []reflect.Type) []byte {
	out := new(bytes.Buffer)

	fmt.Fprintln(out, "# Automatically generated with go run bincodegen.")
	fmt.Fprintln(out, "# Run `go generate ./...` from the go/ directory to regenerate it.")

	fmt.Fprintln(out, "")
	fmt.Fprintln(out, "import enum")
	fmt.Fprintln(out, "from dataclasses import dataclass")
	fmt.Fprintln(out, "import bincode")
	fmt.Fprintln(out, "from typing import ClassVar, List, Any, Union, Tuple, Type")
	fmt.Fprintln(out, "from common import *")

	fmt.Fprintln(out)

	generatePythonErrorCodes(out, errors)

	generatePythonMsgKind(out, "Shard", shardReqResps)
	generatePythonMsgKind(out, "CDC", cdcReqResps)

	for _, typ := range extras {
		generatePythonSingle(out, typ, "")
	}
	for _, reqResp := range shardReqResps {
		generatePythonSingle(out, reqResp.req, "Shard")
		generatePythonSingle(out, reqResp.resp, "Shard")
	}
	for _, reqResp := range cdcReqResps {
		generatePythonSingle(out, reqResp.req, "CDC")
		generatePythonSingle(out, reqResp.resp, "CDC")
	}

	generatePythonReqRespUnion(out, "Shard", shardReqResps)
	generatePythonKindToReqResp(out, "Shard", shardReqResps)

	generatePythonReqRespUnion(out, "CDC", cdcReqResps)
	generatePythonKindToReqResp(out, "CDC", cdcReqResps)

	return out.Bytes()
}

func generateC(errors []string, shardReqResps []reqRespType, cdcReqResps []reqRespType, extras []reflect.Type) []byte {
	out := new(bytes.Buffer)

	fmt.Fprintln(out, "// Automatically generated with go run bincodegen.")
	fmt.Fprintln(out, "// Run `go generate ./...` from the go/ directory to regenerate it.")

	for i, err := range errors {
		fmt.Fprintf(out, "#define EGGSFS_ERR_%s %d\n", err, errCodeOffset+i)
	}
	fmt.Fprintf(out, "\n")

	for _, reqResp := range shardReqResps {
		fmt.Fprintf(out, "#define EGGSFS_META_%s 0x%X\n", reqRespEnum(reqResp), reqResp.kind)
	}
	fmt.Fprintf(out, "\n")

	for _, reqResp := range cdcReqResps {
		fmt.Fprintf(out, "#define EGGSFS_CDC_%s 0x%X\n", reqRespEnum(reqResp), reqResp.kind)
	}
	fmt.Fprintf(out, "\n")

	return out.Bytes()
}

func cppType(t reflect.Type) string {
	if t.Name() == "InodeId" || t.Name() == "InodeIdExtra" || t.Name() == "Parity" || t.Name() == "EggsTime" {
		return t.Name()
	}
	switch t.Kind() {
	case reflect.Uint8:
		return "uint8_t"
	case reflect.Uint16:
		return "uint16_t"
	case reflect.Uint32:
		return "uint32_t"
	case reflect.Uint64:
		return "uint64_t"
	case reflect.Bool:
		return "bool"
	case reflect.Struct:
		return t.Name()
	case reflect.Slice, reflect.String:
		elem := sliceTypeElem(t)
		if elem.Kind() == reflect.Uint8 {
			return "BincodeBytes"
		} else {
			return fmt.Sprintf("BincodeList<%s>", cppType(elem))
		}
	case reflect.Array:
		return fmt.Sprintf("BincodeFixedBytes<%d>", t.Size())
	default:
		panic(fmt.Sprintf("unsupported type with kind %v", t.Kind()))
	}
}

type cppCodegen struct {
	pack           *bytes.Buffer
	unpack         *bytes.Buffer
	clear          *bytes.Buffer
	size           *bytes.Buffer
	eq             *bytes.Buffer
	staticSize     []string
	staticSizeInfo []string
}

func (cg *cppCodegen) pline(s string) {
	cg.pack.Write([]byte(fmt.Sprintf("    %s;\n", s)))
}

func (cg *cppCodegen) uline(s string) {
	cg.unpack.Write([]byte(fmt.Sprintf("    %s;\n", s)))
}

func (cg *cppCodegen) cline(s string) {
	cg.clear.Write([]byte(fmt.Sprintf("    %s;\n", s)))
}

func (cg *cppCodegen) sline(what string, s string) {
	cg.size.Write([]byte(fmt.Sprintf("        _size += %s; // %s\n", s, what)))
}

func (cg *cppCodegen) eline(s string) {
	cg.eq.Write([]byte(fmt.Sprintf("    if (%s) { return false; };\n", s)))
}

func (gc *cppCodegen) addStaticSize(what string, s string, sameAsSize bool) {
	gc.staticSize = append(gc.staticSize, s)
	gc.staticSizeInfo = append(gc.staticSizeInfo, what)
	if sameAsSize {
		gc.sline(what, s)
	}
}

func (cg *cppCodegen) gen(expr *subexpr) {
	k := expr.typ.Kind()

	// pack/unpack
	// we want InodeId/InodeIdExtra/Parity to be here because of some checks we perform
	// when unpacking
	if k == reflect.Struct || expr.typ.Name() == "InodeId" || expr.typ.Name() == "InodeIdExtra" || expr.typ.Name() == "Parity" || expr.typ.Name() == "EggsTime" {
		cg.pline(fmt.Sprintf("%s.pack(buf)", expr.fld))
		cg.uline(fmt.Sprintf("%s.unpack(buf)", expr.fld))
	} else if k == reflect.Uint64 && expr.tag == "varint" {
		cg.pline(fmt.Sprintf("buf.packVarU61(%s)", expr.fld))
		cg.uline(fmt.Sprintf("%s = buf.unpackVarU61()", expr.fld))
	} else if k == reflect.Bool || k == reflect.Uint8 || k == reflect.Uint16 || k == reflect.Uint32 || k == reflect.Uint64 {
		cg.pline(fmt.Sprintf("buf.packScalar<%s>(%s)", cppType(expr.typ), expr.fld))
		cg.uline(fmt.Sprintf("%s = buf.unpackScalar<%s>()", expr.fld, cppType(expr.typ)))
	} else if k == reflect.Array {
		elem := sliceTypeElem(expr.typ)
		if elem.Kind() != reflect.Uint8 {
			panic(fmt.Sprintf("we only support arrays of bytes, got %v", elem.Kind()))
		}
		len := expr.typ.Size()
		cg.pline(fmt.Sprintf("buf.packFixedBytes<%d>(%s)", len, expr.fld))
		cg.uline(fmt.Sprintf("buf.unpackFixedBytes<%d>(%s)", len, expr.fld))
	} else if k == reflect.Slice || k == reflect.String {
		elem := sliceTypeElem(expr.typ)
		if elem.Kind() == reflect.Uint8 {
			cg.pline(fmt.Sprintf("buf.packBytes(%s)", expr.fld))
			cg.uline(fmt.Sprintf("buf.unpackBytes(%s)", expr.fld))
		} else {
			cg.pline(fmt.Sprintf("buf.packList<%s>(%s)", cppType(elem), expr.fld))
			cg.uline(fmt.Sprintf("buf.unpackList<%s>(%s)", cppType(elem), expr.fld))
		}
	} else {
		panic(fmt.Errorf("unexpected kind %v", k))
	}

	// clear/eq
	switch k {
	case reflect.Bool, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		if expr.typ.Name() == "ShardId" || expr.typ.Name() == "InodeId" || expr.typ.Name() == "InodeIdExtra" || expr.typ.Name() == "Parity" || expr.typ.Name() == "EggsTime" {
			cg.cline(fmt.Sprintf("%s = %s()", expr.fld, cppType(expr.typ)))
		} else {
			cg.cline(fmt.Sprintf("%s = %s(0)", expr.fld, cppType(expr.typ)))
		}
		cg.eline(fmt.Sprintf("(%s)this->%s != (%s)rhs.%s", cppType(expr.typ), expr.fld, cppType(expr.typ), expr.fld))
	case reflect.Array, reflect.Slice, reflect.Struct, reflect.String:
		cg.cline(fmt.Sprintf("%s.clear()", expr.fld))
		cg.eline(fmt.Sprintf("%s != rhs.%s", expr.fld, expr.fld))
	}

	// size
	if k == reflect.Uint64 && expr.tag == "varint" {
		cg.sline(expr.fld, fmt.Sprintf("varU61Size(%s)", expr.fld))
	} else if k == reflect.Array {
		cg.addStaticSize(expr.fld, fmt.Sprintf("%s::STATIC_SIZE", cppType(expr.typ)), true)
	} else if k == reflect.Slice || k == reflect.Struct || k == reflect.String {
		cg.addStaticSize(expr.fld, fmt.Sprintf("%s::STATIC_SIZE", cppType(expr.typ)), false)
		cg.sline(expr.fld, fmt.Sprintf("%s.packedSize()", expr.fld))
	} else {
		var sz int
		switch k {
		case reflect.Bool, reflect.Uint8:
			sz = 1
		case reflect.Uint16:
			sz = 2
		case reflect.Uint32:
			sz = 4
		case reflect.Uint64:
			sz = 8
		default:
			panic(fmt.Errorf("unexpected kind %v", k))
		}
		cg.addStaticSize(expr.fld, fmt.Sprintf("%d", sz), true)
	}
}

func cppFieldName(fld reflect.StructField) string {
	nameBytes := []rune(fld.Name)
	nameBytes[0] = unicode.ToLower(nameBytes[0])
	return string(nameBytes)
}

func generateCppSingle(hpp io.Writer, cpp io.Writer, t reflect.Type) {
	if t.Kind() != reflect.Struct {
		panic(fmt.Sprintf("type %v is not a struct", t))
	}

	cg := cppCodegen{
		pack:   new(bytes.Buffer),
		unpack: new(bytes.Buffer),
		clear:  new(bytes.Buffer),
		size:   new(bytes.Buffer),
		eq:     new(bytes.Buffer),
	}

	fmt.Fprintf(hpp, "struct %s {\n", t.Name())
	for i := 0; i < t.NumField(); i++ {
		fld := t.Field(i)
		name := cppFieldName(fld)
		fmt.Fprintf(hpp, "    %s %s;\n", cppType(fld.Type), name)
		cg.gen(&subexpr{
			typ: fld.Type,
			tag: parseTag(fld.Tag),
			fld: name,
		})
	}
	if len(cg.staticSize) == 0 {
		cg.staticSize = append(cg.staticSize, "0")
	}
	fmt.Fprintf(hpp, "\n")
	fmt.Fprintf(hpp, "    static constexpr uint16_t STATIC_SIZE = %s; // %s\n", strings.Join(cg.staticSize, " + "), strings.Join(cg.staticSizeInfo, " + "))
	fmt.Fprintf(hpp, "\n")
	fmt.Fprintf(hpp, "    %s() { clear(); }\n\n", t.Name())
	fmt.Fprintf(hpp, "    uint16_t packedSize() const {\n")
	fmt.Fprintf(hpp, "        uint16_t _size = 0;\n")
	hpp.Write(cg.size.Bytes())
	fmt.Fprintf(hpp, "        return _size;\n")
	fmt.Fprintf(hpp, "    }\n")
	fmt.Fprintf(hpp, "    void pack(BincodeBuf& buf) const;\n")
	fmt.Fprintf(hpp, "    void unpack(BincodeBuf& buf);\n")
	fmt.Fprintf(hpp, "    void clear();\n")
	fmt.Fprintf(hpp, "    bool operator==(const %s&rhs) const;\n", t.Name())
	fmt.Fprintf(hpp, "};\n\n")

	fmt.Fprintf(cpp, "void %s::pack(BincodeBuf& buf) const {\n", t.Name())
	cpp.Write(cg.pack.Bytes())
	fmt.Fprintf(cpp, "}\n")
	fmt.Fprintf(cpp, "void %s::unpack(BincodeBuf& buf) {\n", t.Name())
	cpp.Write(cg.unpack.Bytes())
	fmt.Fprintf(cpp, "}\n")
	fmt.Fprintf(cpp, "void %s::clear() {\n", t.Name())
	cpp.Write(cg.clear.Bytes())
	fmt.Fprintf(cpp, "}\n")

	fmt.Fprintf(hpp, "std::ostream& operator<<(std::ostream& out, const %s& x);\n\n", t.Name())

	fmt.Fprintf(cpp, "bool %s::operator==(const %s& rhs) const {\n", t.Name(), t.Name())
	cpp.Write(cg.eq.Bytes())
	fmt.Fprintf(cpp, "    return true;\n")
	fmt.Fprintf(cpp, "}\n")

	fmt.Fprintf(cpp, "std::ostream& operator<<(std::ostream& out, const %s& x) {\n", t.Name())
	fmt.Fprintf(cpp, "    out << \"%s(\"", t.Name())
	for i := 0; i < t.NumField(); i++ {
		if i > 0 {
			fmt.Fprintf(cpp, " << \", \"")
		}
		fld := t.Field(i)
		if cppType(fld.Type) == "uint8_t" {
			fmt.Fprintf(cpp, " << \"%s=\" << (int)x.%s", fld.Name, cppFieldName(fld))
		} else {
			fmt.Fprintf(cpp, " << \"%s=\" << x.%s", fld.Name, cppFieldName(fld))
		}
	}
	fmt.Fprintf(cpp, " << \")\";\n")
	fmt.Fprintf(cpp, "    return out;\n")
	fmt.Fprintf(cpp, "}\n\n")
}

func generateCppKind(hpp io.Writer, cpp io.Writer, name string, reqResps []reqRespType) {
	fmt.Fprintf(hpp, "enum class %sMessageKind : uint8_t {\n", name)
	fmt.Fprintf(hpp, "    ERROR = 0,\n")
	for _, reqResp := range reqResps {
		fmt.Fprintf(hpp, "    %s = %d,\n", reqRespEnum(reqResp), reqResp.kind)
	}
	fmt.Fprintf(hpp, "};\n\n")

	fmt.Fprintf(hpp, "std::ostream& operator<<(std::ostream& out, %sMessageKind kind);\n\n", name)

	fmt.Fprintf(cpp, "std::ostream& operator<<(std::ostream& out, %sMessageKind kind) {\n", name)
	fmt.Fprintf(cpp, "    switch (kind) {\n")
	for _, reqResp := range reqResps {
		fmt.Fprintf(cpp, "    case %sMessageKind::%s:\n", name, reqRespEnum(reqResp))
		fmt.Fprintf(cpp, "        out << \"%s\";\n", reqRespEnum(reqResp))
		fmt.Fprintf(cpp, "        break;\n")
	}
	fmt.Fprintf(cpp, "    default:\n")
	fmt.Fprintf(cpp, "        out << \"%sMessageKind(\" << ((int)kind) << \")\";\n", name)
	fmt.Fprintf(cpp, "        break;\n")
	fmt.Fprintf(cpp, "    }\n")
	fmt.Fprintf(cpp, "    return out;\n")
	fmt.Fprintf(cpp, "}\n\n")
}

type containerType struct {
	name string
	enum string
	typ  reflect.Type
}

func generateCppContainer(hpp io.Writer, cpp io.Writer, name string, kindTypeName string, types []containerType) {
	fmt.Fprintf(hpp, "struct %s {\n", name)
	fmt.Fprintf(hpp, "private:\n")
	fmt.Fprintf(hpp, "    %s _kind = (%s)0;\n", kindTypeName, kindTypeName)
	fmt.Fprintf(hpp, "    std::tuple<")
	for i, typ := range types {
		if i > 0 {
			fmt.Fprintf(hpp, ", ")
		}
		hpp.Write([]byte(cppType(typ.typ)))
	}
	fmt.Fprintf(hpp, "> _data;\n")
	fmt.Fprintf(hpp, "public:\n")
	fmt.Fprintf(hpp, "    %s kind() const { return _kind; }\n", kindTypeName)
	for i, typ := range types {
		fmt.Fprintf(hpp, "    const %s& get%s() const;\n", cppType(typ.typ), typ.name)
		fmt.Fprintf(hpp, "    %s& set%s();\n", cppType(typ.typ), typ.name)
		fmt.Fprintf(cpp, "const %s& %s::get%s() const {\n", cppType(typ.typ), name, typ.name)
		fmt.Fprintf(cpp, "    ALWAYS_ASSERT(_kind == %s::%s, \"%%s != %%s\", _kind, %s::%s);\n", kindTypeName, typ.enum, kindTypeName, typ.enum)
		fmt.Fprintf(cpp, "    return std::get<%d>(_data);\n", i)
		fmt.Fprintf(cpp, "}\n")
		fmt.Fprintf(cpp, "%s& %s::set%s() {\n", cppType(typ.typ), name, typ.name)
		fmt.Fprintf(cpp, "    _kind = %s::%s;\n", kindTypeName, typ.enum)
		fmt.Fprintf(cpp, "    auto& x = std::get<%d>(_data);\n", i)
		fmt.Fprintf(cpp, "    x.clear();\n")
		fmt.Fprintf(cpp, "    return x;\n")
		fmt.Fprintf(cpp, "}\n")
	}
	fmt.Fprintf(hpp, "\n")
	fmt.Fprintf(hpp, "    void clear() { _kind = (%s)0; };\n\n", kindTypeName)
	fmt.Fprintf(hpp, "    void pack(BincodeBuf& buf) const;\n")
	fmt.Fprintf(hpp, "    void unpack(BincodeBuf& buf, %s kind);\n", kindTypeName)
	fmt.Fprintf(hpp, "};\n\n")

	fmt.Fprintf(hpp, "std::ostream& operator<<(std::ostream& out, const %s& x);\n\n", name)

	fmt.Fprintf(cpp, "void %s::pack(BincodeBuf& buf) const {\n", name)
	fmt.Fprintf(cpp, "    switch (_kind) {\n")
	for i, typ := range types {
		fmt.Fprintf(cpp, "    case %s::%s:\n", kindTypeName, typ.enum)
		fmt.Fprintf(cpp, "        std::get<%d>(_data).pack(buf);\n", i)
		fmt.Fprintf(cpp, "        break;\n")
	}
	fmt.Fprintf(cpp, "    default:\n")
	fmt.Fprintf(cpp, "        throw EGGS_EXCEPTION(\"bad %s kind %%s\", _kind);\n", kindTypeName)
	fmt.Fprintf(cpp, "    }\n")
	fmt.Fprintf(cpp, "}\n\n")
	fmt.Fprintf(cpp, "void %s::unpack(BincodeBuf& buf, %s kind) {\n", name, kindTypeName)
	fmt.Fprintf(cpp, "    _kind = kind;\n")
	fmt.Fprintf(cpp, "    switch (kind) {\n")
	for i, typ := range types {
		fmt.Fprintf(cpp, "    case %s::%s:\n", kindTypeName, typ.enum)
		fmt.Fprintf(cpp, "        std::get<%d>(_data).unpack(buf);\n", i)
		fmt.Fprintf(cpp, "        break;\n")
	}
	fmt.Fprintf(cpp, "    default:\n")
	fmt.Fprintf(cpp, "        throw BINCODE_EXCEPTION(\"bad %s kind %%s\", kind);\n", kindTypeName)
	fmt.Fprintf(cpp, "    }\n")
	fmt.Fprintf(cpp, "}\n\n")
	fmt.Fprintf(cpp, "std::ostream& operator<<(std::ostream& out, const %s& x) {\n", name)
	fmt.Fprintf(cpp, "    switch (x.kind()) {\n")
	for _, typ := range types {
		fmt.Fprintf(cpp, "    case %s::%s:\n", kindTypeName, typ.enum)
		fmt.Fprintf(cpp, "        out << x.get%s();\n", typ.name)
		fmt.Fprintf(cpp, "        break;\n")
	}
	fmt.Fprintf(cpp, "    default:\n")
	fmt.Fprintf(cpp, "        throw EGGS_EXCEPTION(\"bad %s kind %%s\", x.kind());\n", kindTypeName)
	fmt.Fprintf(cpp, "    }\n")
	fmt.Fprintf(cpp, "    return out;\n")
	fmt.Fprintf(cpp, "}\n\n")
}

func generateCppLogEntries(hpp io.Writer, cpp io.Writer, what string, types []reflect.Type) {
	containerTypes := make([]containerType, len(types))
	fmt.Fprintf(hpp, "enum class %sLogEntryKind : uint16_t {\n", what)
	for i, typ := range types {
		fmt.Fprintf(hpp, "    %s = %d,\n", enumName(typ), i+1) // skip 0 since we use it as an empty value
		containerTypes[i].typ = typ
		containerTypes[i].enum = enumName(typ)
		if !strings.HasSuffix(typ.Name(), "Entry") {
			panic(fmt.Errorf("bad log entry type %s", typ.Name()))
		}
		containerTypes[i].name = string([]byte(types[i].Name())[:len(types[i].Name())-len("Entry")])
	}
	fmt.Fprintf(hpp, "};\n\n")
	fmt.Fprintf(hpp, "std::ostream& operator<<(std::ostream& out, %sLogEntryKind err);\n\n", what)
	fmt.Fprintf(cpp, "std::ostream& operator<<(std::ostream& out, %sLogEntryKind err) {\n", what)
	fmt.Fprintf(cpp, "    switch (err) {\n")
	for _, typ := range containerTypes {
		fmt.Fprintf(cpp, "    case %sLogEntryKind::%s:\n", what, typ.enum)
		fmt.Fprintf(cpp, "        out << \"%s\";\n", typ.enum)
		fmt.Fprintf(cpp, "        break;\n")
	}
	fmt.Fprintf(cpp, "    default:\n")
	fmt.Fprintf(cpp, "        out << \"%sLogEntryKind(\" << ((int)err) << \")\";\n", what)
	fmt.Fprintf(cpp, "        break;\n")
	fmt.Fprintf(cpp, "    }\n")
	fmt.Fprintf(cpp, "    return out;\n")
	fmt.Fprintf(cpp, "}\n\n")

	for _, typ := range types {
		generateCppSingle(hpp, cpp, typ)
	}
	generateCppContainer(hpp, cpp, what+"LogEntryContainer", what+"LogEntryKind", containerTypes)
}

func generateCppReqResp(hpp io.Writer, cpp io.Writer, what string, reqResps []reqRespType) {
	generateCppKind(hpp, cpp, what, reqResps)
	reqContainerTypes := make([]containerType, len(reqResps))
	for i, reqResp := range reqResps {
		reqContainerTypes[i] = containerType{
			name: string([]byte(reqResp.req.Name())[:len(reqResp.req.Name())-len("Req")]),
			enum: reqRespEnum(reqResp),
			typ:  reqResp.req,
		}
	}
	generateCppContainer(hpp, cpp, what+"ReqContainer", what+"MessageKind", reqContainerTypes)
	respContainerTypes := make([]containerType, len(reqResps))
	for i, reqResp := range reqResps {
		respContainerTypes[i] = containerType{
			name: string([]byte(reqResp.resp.Name())[:len(reqResp.resp.Name())-len("Resp")]),
			enum: reqRespEnum(reqResp),
			typ:  reqResp.resp,
		}
	}
	generateCppContainer(hpp, cpp, what+"RespContainer", what+"MessageKind", respContainerTypes)
}

func generateCpp(errors []string, shardReqResps []reqRespType, cdcReqResps []reqRespType, extras []reflect.Type) ([]byte, []byte) {
	hppOut := new(bytes.Buffer)
	cppOut := new(bytes.Buffer)

	fmt.Fprintln(hppOut, "// Automatically generated with go run bincodegen.")
	fmt.Fprintln(hppOut, "// Run `go generate ./...` from the go/ directory to regenerate it.")
	fmt.Fprintln(hppOut, "#pragma once")
	fmt.Fprintln(hppOut, "#include \"Msgs.hpp\"")
	fmt.Fprintln(hppOut)

	fmt.Fprintln(cppOut, "// Automatically generated with go run bincodegen.")
	fmt.Fprintln(cppOut, "// Run `go generate ./...` from the go/ directory to regenerate it.")
	fmt.Fprintln(cppOut, "#include \"Msgs.hpp\"")
	fmt.Fprintln(cppOut)

	fmt.Fprintf(hppOut, "enum class EggsError : uint16_t {\n")
	for i, err := range errors {
		fmt.Fprintf(hppOut, "    %s = %d,\n", err, errCodeOffset+i)
	}
	fmt.Fprintf(hppOut, "};\n\n")
	fmt.Fprintf(hppOut, "std::ostream& operator<<(std::ostream& out, EggsError err);\n\n")
	fmt.Fprintf(cppOut, "std::ostream& operator<<(std::ostream& out, EggsError err) {\n")
	fmt.Fprintf(cppOut, "    switch (err) {\n")
	for _, err := range errors {
		fmt.Fprintf(cppOut, "    case EggsError::%s:\n", err)
		fmt.Fprintf(cppOut, "        out << \"%s\";\n", err)
		fmt.Fprintf(cppOut, "        break;\n")
	}
	fmt.Fprintf(cppOut, "    default:\n")
	fmt.Fprintf(cppOut, "        out << \"EggsError(\" << ((int)err) << \")\";\n")
	fmt.Fprintf(cppOut, "        break;\n")
	fmt.Fprintf(cppOut, "    }\n")
	fmt.Fprintf(cppOut, "    return out;\n")
	fmt.Fprintf(cppOut, "}\n\n")

	for _, typ := range extras {
		generateCppSingle(hppOut, cppOut, typ)
	}
	for _, reqResp := range shardReqResps {
		generateCppSingle(hppOut, cppOut, reqResp.req)
		generateCppSingle(hppOut, cppOut, reqResp.resp)
	}
	for _, reqResp := range cdcReqResps {
		generateCppSingle(hppOut, cppOut, reqResp.req)
		generateCppSingle(hppOut, cppOut, reqResp.resp)
	}

	generateCppReqResp(hppOut, cppOut, "Shard", shardReqResps)
	generateCppReqResp(hppOut, cppOut, "CDC", cdcReqResps)

	generateCppLogEntries(
		hppOut,
		cppOut,
		"Shard",
		[]reflect.Type{
			reflect.TypeOf(msgs.ConstructFileEntry{}),
			reflect.TypeOf(msgs.LinkFileEntry{}),
			reflect.TypeOf(msgs.SameDirectoryRenameEntry{}),
			reflect.TypeOf(msgs.SoftUnlinkFileEntry{}),
			reflect.TypeOf(msgs.CreateDirectoryInodeEntry{}),
			reflect.TypeOf(msgs.CreateLockedCurrentEdgeEntry{}),
			reflect.TypeOf(msgs.UnlockCurrentEdgeEntry{}),
			reflect.TypeOf(msgs.LockCurrentEdgeEntry{}),
			reflect.TypeOf(msgs.RemoveDirectoryOwnerEntry{}),
			reflect.TypeOf(msgs.RemoveInodeEntry{}),
			reflect.TypeOf(msgs.SetDirectoryOwnerEntry{}),
			reflect.TypeOf(msgs.SetDirectoryInfoEntry{}),
			reflect.TypeOf(msgs.RemoveNonOwnedEdgeEntry{}),
			reflect.TypeOf(msgs.IntraShardHardFileUnlinkEntry{}),
			reflect.TypeOf(msgs.RemoveSpanInitiateEntry{}),
			reflect.TypeOf(msgs.UpdateBlockServicesEntry{}),
			reflect.TypeOf(msgs.AddSpanInitiateEntry{}),
			reflect.TypeOf(msgs.AddSpanCertifyEntry{}),
		},
	)

	/*
		generateCppLogEntries(
			hppOut,
			cppOut,
			"CDC",
			[]reflect.Type{},
		)
	*/

	return hppOut.Bytes(), cppOut.Bytes()
}

func main() {
	cwd, err := os.Getwd()
	if err != nil {
		panic(err)
	}

	errors := []string{
		"INTERNAL_ERROR",
		"FATAL_ERROR",
		"TIMEOUT",
		"MALFORMED_REQUEST",
		"MALFORMED_RESPONSE",
		"NOT_AUTHORISED",
		"UNRECOGNIZED_REQUEST",
		"FILE_NOT_FOUND",
		"DIRECTORY_NOT_FOUND",
		"NAME_NOT_FOUND",
		"TYPE_IS_DIRECTORY",
		"TYPE_IS_NOT_DIRECTORY",
		"BAD_COOKIE",
		"INCONSISTENT_STORAGE_CLASS_PARITY",
		"LAST_SPAN_STATE_NOT_CLEAN",
		"COULD_NOT_PICK_BLOCK_SERVICES",
		"BAD_SPAN_BODY",
		"SPAN_NOT_FOUND",
		"BLOCK_SERVICE_NOT_FOUND",
		"CANNOT_CERTIFY_BLOCKLESS_SPAN",
		"BAD_NUMBER_OF_BLOCKS_PROOFS",
		"BAD_BLOCK_PROOF",
		"CANNOT_OVERRIDE_NAME",
		"NAME_IS_LOCKED",
		"OLD_NAME_IS_LOCKED",
		"NEW_NAME_IS_LOCKED",
		"MTIME_IS_TOO_RECENT",
		"MISMATCHING_TARGET",
		"MISMATCHING_OWNER",
		"DIRECTORY_NOT_EMPTY",
		"FILE_IS_TRANSIENT",
		"OLD_DIRECTORY_NOT_FOUND",
		"NEW_DIRECTORY_NOT_FOUND",
		"LOOP_IN_DIRECTORY_RENAME",
		"EDGE_NOT_FOUND",
		"DIRECTORY_HAS_OWNER",
		"FILE_IS_NOT_TRANSIENT",
		"FILE_NOT_EMPTY",
		"CANNOT_REMOVE_ROOT_DIRECTORY",
		"FILE_EMPTY",
		"CANNOT_REMOVE_DIRTY_SPAN",
		"BAD_SHARD",
		"BAD_NAME",
		"MORE_RECENT_SNAPSHOT_EDGE",
		"MORE_RECENT_CURRENT_EDGE",
		"BAD_DIRECTORY_INFO",
		"CREATION_TIME_TOO_RECENT",
		"DEADLINE_NOT_PASSED",
	}

	shardReqResps := []reqRespType{
		{
			0x01,
			reflect.TypeOf(msgs.LookupReq{}),
			reflect.TypeOf(msgs.LookupResp{}),
		},
		{
			0x02,
			reflect.TypeOf(msgs.StatFileReq{}),
			reflect.TypeOf(msgs.StatFileResp{}),
		},
		{
			0x0A,
			reflect.TypeOf(msgs.StatTransientFileReq{}),
			reflect.TypeOf(msgs.StatTransientFileResp{}),
		},
		{
			0x08,
			reflect.TypeOf(msgs.StatDirectoryReq{}),
			reflect.TypeOf(msgs.StatDirectoryResp{}),
		},
		{
			0x03,
			reflect.TypeOf(msgs.ReadDirReq{}),
			reflect.TypeOf(msgs.ReadDirResp{}),
		},
		{
			0x04,
			reflect.TypeOf(msgs.ConstructFileReq{}),
			reflect.TypeOf(msgs.ConstructFileResp{}),
		},
		{
			0x05,
			reflect.TypeOf(msgs.AddSpanInitiateReq{}),
			reflect.TypeOf(msgs.AddSpanInitiateResp{}),
		},
		{
			0x06,
			reflect.TypeOf(msgs.AddSpanCertifyReq{}),
			reflect.TypeOf(msgs.AddSpanCertifyResp{}),
		},
		{
			0x07,
			reflect.TypeOf(msgs.LinkFileReq{}),
			reflect.TypeOf(msgs.LinkFileResp{}),
		},
		{
			0x0C,
			reflect.TypeOf(msgs.SoftUnlinkFileReq{}),
			reflect.TypeOf(msgs.SoftUnlinkFileResp{}),
		},
		{
			0x0D,
			reflect.TypeOf(msgs.FileSpansReq{}),
			reflect.TypeOf(msgs.FileSpansResp{}),
		},
		{
			0x0E,
			reflect.TypeOf(msgs.SameDirectoryRenameReq{}),
			reflect.TypeOf(msgs.SameDirectoryRenameResp{}),
		},
		{
			0x0F,
			reflect.TypeOf(msgs.SetDirectoryInfoReq{}),
			reflect.TypeOf(msgs.SetDirectoryInfoResp{}),
		},
		// PRIVATE OPERATIONS -- These are safe operations, but we don't want the FS client itself
		// to perform them. TODO make privileged?
		{
			0x15,
			reflect.TypeOf(msgs.VisitDirectoriesReq{}),
			reflect.TypeOf(msgs.VisitDirectoriesResp{}),
		},
		{
			0x20,
			reflect.TypeOf(msgs.VisitFilesReq{}),
			reflect.TypeOf(msgs.VisitFilesResp{}),
		},
		{
			0x16,
			reflect.TypeOf(msgs.VisitTransientFilesReq{}),
			reflect.TypeOf(msgs.VisitTransientFilesResp{}),
		},
		{
			0x21,
			reflect.TypeOf(msgs.FullReadDirReq{}),
			reflect.TypeOf(msgs.FullReadDirResp{}),
		},
		{
			0x17,
			reflect.TypeOf(msgs.RemoveNonOwnedEdgeReq{}),
			reflect.TypeOf(msgs.RemoveNonOwnedEdgeResp{}),
		},
		{
			0x18,
			reflect.TypeOf(msgs.IntraShardHardFileUnlinkReq{}),
			reflect.TypeOf(msgs.IntraShardHardFileUnlinkResp{}),
		},
		{
			0x19,
			reflect.TypeOf(msgs.RemoveSpanInitiateReq{}),
			reflect.TypeOf(msgs.RemoveSpanInitiateResp{}),
		},
		{
			0x1A,
			reflect.TypeOf(msgs.RemoveSpanCertifyReq{}),
			reflect.TypeOf(msgs.RemoveSpanCertifyResp{}),
		},
		{
			0x22,
			reflect.TypeOf(msgs.SwapBlocksReq{}),
			reflect.TypeOf(msgs.SwapBlocksResp{}),
		},
		{
			0x23,
			reflect.TypeOf(msgs.BlockServiceFilesReq{}),
			reflect.TypeOf(msgs.BlockServiceFilesResp{}),
		},
		{
			0x24,
			reflect.TypeOf(msgs.RemoveInodeReq{}),
			reflect.TypeOf(msgs.RemoveInodeResp{}),
		},
		// UNSAFE OPERATIONS -- these can break invariants.
		{
			0x80,
			reflect.TypeOf(msgs.CreateDirectoryInodeReq{}),
			reflect.TypeOf(msgs.CreateDirectoryInodeResp{}),
		},
		{
			0x81,
			reflect.TypeOf(msgs.SetDirectoryOwnerReq{}),
			reflect.TypeOf(msgs.SetDirectoryOwnerResp{}),
		},
		{
			0x89,
			reflect.TypeOf(msgs.RemoveDirectoryOwnerReq{}),
			reflect.TypeOf(msgs.RemoveDirectoryOwnerResp{}),
		},
		{
			0x82,
			reflect.TypeOf(msgs.CreateLockedCurrentEdgeReq{}),
			reflect.TypeOf(msgs.CreateLockedCurrentEdgeResp{}),
		},
		{
			0x83,
			reflect.TypeOf(msgs.LockCurrentEdgeReq{}),
			reflect.TypeOf(msgs.LockCurrentEdgeResp{}),
		},
		{
			0x84,
			reflect.TypeOf(msgs.UnlockCurrentEdgeReq{}),
			reflect.TypeOf(msgs.UnlockCurrentEdgeResp{}),
		},
		{
			0x86,
			reflect.TypeOf(msgs.RemoveOwnedSnapshotFileEdgeReq{}),
			reflect.TypeOf(msgs.RemoveOwnedSnapshotFileEdgeResp{}),
		},
		{
			0x87,
			reflect.TypeOf(msgs.MakeFileTransientReq{}),
			reflect.TypeOf(msgs.MakeFileTransientResp{}),
		},
	}

	cdcReqResps := []reqRespType{
		{
			0x01,
			reflect.TypeOf(msgs.MakeDirectoryReq{}),
			reflect.TypeOf(msgs.MakeDirectoryResp{}),
		},
		{
			0x02,
			reflect.TypeOf(msgs.RenameFileReq{}),
			reflect.TypeOf(msgs.RenameFileResp{}),
		},
		{
			0x03,
			reflect.TypeOf(msgs.SoftUnlinkDirectoryReq{}),
			reflect.TypeOf(msgs.SoftUnlinkDirectoryResp{}),
		},
		{
			0x04,
			reflect.TypeOf(msgs.RenameDirectoryReq{}),
			reflect.TypeOf(msgs.RenameDirectoryResp{}),
		},
		{
			0x05,
			reflect.TypeOf(msgs.HardUnlinkDirectoryReq{}),
			reflect.TypeOf(msgs.HardUnlinkDirectoryResp{}),
		},
		{
			0x06,
			reflect.TypeOf(msgs.HardUnlinkFileReq{}),
			reflect.TypeOf(msgs.HardUnlinkFileResp{}),
		},
	}

	extras := []reflect.Type{
		reflect.TypeOf(msgs.TransientFile{}),
		reflect.TypeOf(msgs.FetchedBlock{}),
		reflect.TypeOf(msgs.CurrentEdge{}),
		reflect.TypeOf(msgs.Edge{}),
		reflect.TypeOf(msgs.FetchedSpan{}),
		reflect.TypeOf(msgs.BlockInfo{}),
		reflect.TypeOf(msgs.NewBlockInfo{}),
		reflect.TypeOf(msgs.BlockProof{}),
		reflect.TypeOf(msgs.SpanPolicy{}),
		reflect.TypeOf(msgs.DirectoryInfoBody{}),
		reflect.TypeOf(msgs.SetDirectoryInfo{}),
		reflect.TypeOf(msgs.BlockServiceBlacklist{}),
		reflect.TypeOf(msgs.BlockService{}),
		reflect.TypeOf(msgs.FullReadDirCursor{}),
		reflect.TypeOf(msgs.EntryBlockService{}),
		reflect.TypeOf(msgs.EntryNewBlockInfo{}),
	}

	goCode := generateGo(errors, shardReqResps, cdcReqResps, extras)
	goOutFileName := fmt.Sprintf("%s/msgs_bincode.go", cwd)
	goOutFile, err := os.Create(goOutFileName)
	if err != nil {
		panic(err)
	}
	defer goOutFile.Close()
	goOutFile.Write(goCode)

	pythonOutFileName := fmt.Sprintf("%s/../../python/msgs.py", cwd)
	pythonOutFile, err := os.Create(pythonOutFileName)
	if err != nil {
		panic(err)
	}
	defer pythonOutFile.Close()
	pythonOutFile.Write(generatePython(errors, shardReqResps, cdcReqResps, extras))

	cOutFilename := fmt.Sprintf("%s/../../c/eggs_msgs.h", cwd)
	cOutFile, err := os.Create(cOutFilename)
	if err != nil {
		panic(err)
	}
	defer cOutFile.Close()
	cOutFile.Write(generateC(errors, shardReqResps, cdcReqResps, extras))

	hppOutFilename := fmt.Sprintf("%s/../../cpp/MsgsGen.hpp", cwd)
	hppOutFile, err := os.Create(hppOutFilename)
	if err != nil {
		panic(err)
	}
	defer hppOutFile.Close()
	cppOutFilename := fmt.Sprintf("%s/../../cpp/MsgsGen.cpp", cwd)
	cppOutFile, err := os.Create(cppOutFilename)
	if err != nil {
		panic(err)
	}
	defer cppOutFile.Close()
	hppBytes, cppBytes := generateCpp(errors, shardReqResps, cdcReqResps, extras)
	hppOutFile.Write(hppBytes)
	cppOutFile.Write(cppBytes)

}
