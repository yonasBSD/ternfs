package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"reflect"
	"regexp"
	"strconv"
	"strings"
	"xtx/eggsfs/msgs"
)

type subexpr struct {
	typ  reflect.Type
	tag  string
	expr string
}

func assertExpectedTag(e *subexpr, expected string) {
	if e.tag != "" {
		panic(fmt.Sprintf("expected %s for subexpr %s", expected, e.expr))
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
func (cg goCodegen) genIndent(expr *subexpr) {
	cg.tabs = cg.tabs[:len(cg.tabs)+1]
	cg.gen(expr)
}

func (cg *goCodegen) lenVar() string {
	*cg.fresh = *cg.fresh + 1
	return fmt.Sprintf("len%d", *cg.fresh)
}

// If -1, it's not fixed bytes. Otherwise, the fixed length.
func fixedBytesLen(e *subexpr) int {
	r := regexp.MustCompile("fixed([1-9][0-9]*)")
	match := r.FindStringSubmatch(e.tag)
	if match == nil {
		assertExpectedTag(e, "tag fixedN or no tag")
		return -1
	}
	len, err := strconv.Atoi(match[1])
	if err != nil {
		panic(fmt.Sprintf("unexpected error in int conversion %v", err))
	}
	return len
}

func (cg *goCodegen) gen(expr *subexpr) {
	t := expr.typ
	switch t.Kind() {
	case reflect.Uint8:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU8(uint8(%v))", expr.expr))
		cg.ustep(fmt.Sprintf("buf.UnpackU8((*uint8)(&%s))", expr.expr))
	case reflect.Bool:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackBool(bool(%v))", expr.expr))
		cg.ustep(fmt.Sprintf("buf.UnpackBool((*bool)(&%s))", expr.expr))
	case reflect.Uint16:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU16(uint16(%v))", expr.expr))
		cg.ustep(fmt.Sprintf("buf.UnpackU16((*uint16)(&%s))", expr.expr))
	case reflect.Uint32:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU32(uint32(%v))", expr.expr))
		cg.ustep(fmt.Sprintf("buf.UnpackU32((*uint32)(&%s))", expr.expr))
	case reflect.Uint64:
		if expr.tag == "varint" {
			cg.pline(fmt.Sprintf("buf.PackVarU61(uint64(%v))", expr.expr))
			cg.ustep(fmt.Sprintf("buf.UnpackVarU61((*uint64)(&%s))", expr.expr))
		} else {
			assertExpectedTag(expr, `tag bincode:"varint" or no tag`)
			cg.pline(fmt.Sprintf("buf.PackU64(uint64(%v))", expr.expr))
			cg.ustep(fmt.Sprintf("buf.UnpackU64((*uint64)(&%s))", expr.expr))
		}
	case reflect.Struct:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("%v.Pack(buf)", expr.expr))
		cg.ustep(fmt.Sprintf("%v.Unpack(buf)", expr.expr))
	case reflect.Slice:
		elem := t.Elem()
		if elem.Kind() == reflect.Uint8 {
			len := fixedBytesLen(expr)
			if len < 0 {
				cg.pline(fmt.Sprintf("buf.PackBytes([]byte(%v))", expr.expr))
				cg.ustep(fmt.Sprintf("buf.UnpackBytes((*[]byte)(&%v))", expr.expr))
			} else {
				cg.pline(fmt.Sprintf("buf.PackFixedBytes(%d, []byte(%v))", len, expr.expr))
				cg.ustep(fmt.Sprintf("buf.UnpackFixedBytes(%d, (*[]byte)(&%v))", len, expr.expr))
			}
		} else {
			lenVar := cg.lenVar()
			// handle length
			cg.pline(fmt.Sprintf("%s := len(%s)", lenVar, expr.expr))
			cg.pline(fmt.Sprintf("buf.PackLength(%s)", lenVar))
			cg.uline(fmt.Sprintf("var %s int", lenVar))
			cg.ustep(fmt.Sprintf("buf.UnpackLength(&%s)", lenVar))
			// handle body
			cg.uline(fmt.Sprintf("bincode.EnsureLength(&%s, %s)", expr.expr, lenVar))
			loop := fmt.Sprintf("for i := 0; i < %s; i++ {", lenVar)
			cg.pline(loop)
			cg.uline(loop)
			cg.genIndent(&subexpr{
				typ:  elem,
				expr: fmt.Sprintf("%s[i]", expr.expr),
				tag:  expr.tag,
			})
			cg.pline("}")
			cg.uline("}")
		}
	default:
		panic(fmt.Sprintf("unsupported type with kind %v", expr.typ.Kind()))
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
		cg.genIndent(&subexpr{
			expr: fmt.Sprintf("v.%s", fld.Name),
			tag:  parseTag(fld.Tag),
			typ:  fld.Type,
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
	if !strings.HasSuffix(tName, "Req") && !strings.HasSuffix(tName, "Resp") {
		panic(fmt.Errorf("bad req/resp name %v", tName))
	}
	trimmed := strings.TrimSuffix(strings.TrimSuffix(tName, "Req"), "Resp")
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
	for _, reqResp := range reqResps {
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
	if t.Name() == "OwnedInodeId" {
		return "InodeIdWithExtra"
	}
	switch t.Kind() {
	case reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		return "int"
	case reflect.Bool:
		return "bool"
	case reflect.Struct:
		return t.Name()
	case reflect.Slice:
		elem := t.Elem()
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
		cg.addStaticSize(expr.expr, "1", true)
		cg.pline(fmt.Sprintf("bincode.pack_u8_into(self.%s, b)", expr.expr))
		cg.uline(fmt.Sprintf("%s = InodeType(bincode.unpack_u8(u))", expr.expr))
		return
	}
	if expr.typ.Name() == "OwnedInodeId" {
		cg.addStaticSize(expr.expr, "8", true)
		cg.pline(fmt.Sprintf("bincode.pack_u64_into(self.%s, b)", expr.expr))
		cg.uline(fmt.Sprintf("%s = InodeIdWithExtra(bincode.unpack_u64(u))", expr.expr))
		return
	}
	switch expr.typ.Kind() {
	case reflect.Uint8:
		cg.addStaticSize(expr.expr, "1", true)
		cg.pline(fmt.Sprintf("bincode.pack_u8_into(self.%s, b)", expr.expr))
		cg.uline(fmt.Sprintf("%s = bincode.unpack_u8(u)", expr.expr))
	case reflect.Bool:
		cg.addStaticSize(expr.expr, "1", true)
		cg.pline(fmt.Sprintf("bincode.pack_u8_into(self.%s, b)", expr.expr))
		cg.uline(fmt.Sprintf("%s = bool(bincode.unpack_u8(u))", expr.expr))
	case reflect.Uint16:
		cg.addStaticSize(expr.expr, "2", true)
		cg.pline(fmt.Sprintf("bincode.pack_u16_into(self.%s, b)", expr.expr))
		cg.uline(fmt.Sprintf("%s = bincode.unpack_u16(u)", expr.expr))
	case reflect.Uint32:
		cg.addStaticSize(expr.expr, "4", true)
		cg.pline(fmt.Sprintf("bincode.pack_u32_into(self.%s, b)", expr.expr))
		cg.uline(fmt.Sprintf("%s = bincode.unpack_u32(u)", expr.expr))
	case reflect.Uint64:
		if expr.tag == "varint" {
			cg.pline(fmt.Sprintf("bincode.pack_v61_into(self.%s, b)", expr.expr))
			cg.uline(fmt.Sprintf("%s = bincode.unpack_v61(u)", expr.expr))
			cg.sadd(expr.expr, fmt.Sprintf("bincode.v61_packed_size(self.%s)", expr.expr))
		} else {
			cg.addStaticSize(expr.expr, "8", true)
			cg.pline(fmt.Sprintf("bincode.pack_u64_into(self.%s, b)", expr.expr))
			cg.uline(fmt.Sprintf("%s = bincode.unpack_u64(u)", expr.expr))
		}
	case reflect.Struct:
		cg.addStaticSize(expr.expr, fmt.Sprintf("%s.STATIC_SIZE", expr.typ.Name()), false)
		cg.sadd(expr.expr, fmt.Sprintf("self.%s.calc_packed_size()", expr.expr))
		cg.pline(fmt.Sprintf("self.%s.pack_into(b)", expr.expr))
		cg.uline(fmt.Sprintf("%s = %s.unpack(u)", expr.expr, expr.typ.Name()))
	case reflect.Slice:
		elem := expr.typ.Elem()
		if elem.Kind() == reflect.Uint8 {
			len := fixedBytesLen(expr)
			if len < 0 {
				cg.addStaticSize(fmt.Sprintf("len(%s)", expr.expr), "1", true) // u8 len
				cg.sadd(fmt.Sprintf("%s contents", expr.expr), fmt.Sprintf("len(self.%s)", expr.expr))
				cg.pline(fmt.Sprintf("bincode.pack_bytes_into(self.%s, b)", expr.expr))
				cg.uline(fmt.Sprintf("%s = bincode.unpack_bytes(u)", expr.expr))
			} else {
				cg.addStaticSize(expr.expr, fmt.Sprintf("%d", len), true)
				cg.pline(fmt.Sprintf("bincode.pack_fixed_into(self.%s, %d, b)", expr.expr, len))
				cg.uline(fmt.Sprintf("%s = bincode.unpack_fixed(u, %d)", expr.expr, len))
			}
		} else {
			cg.addStaticSize(fmt.Sprintf("len(%s)", expr.expr), "2", true) // u16 len
			// pack
			cg.pline(fmt.Sprintf("bincode.pack_u16_into(len(self.%s), b)", expr.expr))
			cg.pline(fmt.Sprintf("for i in range(len(self.%s)):", expr.expr))
			// unpack
			cg.uline(fmt.Sprintf("%s: List[Any] = [None]*bincode.unpack_u16(u)", expr.expr))
			cg.uline(fmt.Sprintf("for i in range(len(%s)):", expr.expr))
			// size
			cg.sline(fmt.Sprintf("for i in range(len(self.%s)):", expr.expr))
			cg.genInSlice(&subexpr{
				typ:  elem,
				expr: fmt.Sprintf("%s[i]", expr.expr),
				tag:  expr.tag,
			})
		}
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
			typ:  fld.Type,
			tag:  parseTag(fld.Tag),
			expr: name,
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

func generateCpp(errors []string, shardReqResps []reqRespType, cdcReqResps []reqRespType, extras []reflect.Type) []byte {
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
		"COULD_NOT_PICK_BLOCK_SERVERS",
		"BAD_SPAN_BODY",
		"SPAN_NOT_FOUND",
		"BLOCK_SERVER_NOT_FOUND",
		"CANNOT_CERTIFY_BLOCKLESS_SPAN",
		"BAD_NUMBER_OF_BLOCKS_PROOFS",
		"BAD_BLOCK_PROOF",
		"CANNOT_OVERRIDE_NAME",
		"NAME_IS_LOCKED",
		"OLD_NAME_IS_LOCKED",
		"NEW_NAME_IS_LOCKED",
		"MORE_RECENT_SNAPSHOT_ALREADY_EXISTS",
		"MISMATCHING_TARGET",
		"MISMATCHING_OWNER",
		"DIRECTORY_NOT_EMPTY",
		"FILE_IS_TRANSIENT",
		"OLD_DIRECTORY_NOT_FOUND",
		"NEW_DIRECTORY_NOT_FOUND",
		"LOOP_IN_DIRECTORY_RENAME",
		"EDGE_NOT_FOUND",
		"CANNOT_CREATE_CURRENT_EDGE_IN_SNAPSHOT_DIRECTORY",
	}

	shardReqResps := []reqRespType{
		// given directory inode and name, returns inode from outgoing
		// current edge.
		{
			0x01,
			reflect.TypeOf(msgs.LookupReq{}),
			reflect.TypeOf(msgs.LookupResp{}),
		},
		// given inode, returns size, type, last modified for files,
		// last modified and parent for directories.
		{
			0x02,
			reflect.TypeOf(msgs.StatReq{}),
			reflect.TypeOf(msgs.StatResp{}),
		},
		{
			0x03,
			reflect.TypeOf(msgs.ReadDirReq{}),
			reflect.TypeOf(msgs.ReadDirResp{}),
		},
		// create a new transient file. takes in desired path of file
		// for debugging purposes
		{
			0x04,
			reflect.TypeOf(msgs.ConstructFileReq{}),
			reflect.TypeOf(msgs.ConstructFileResp{}),
		},
		// add span. the file must be transient
		{
			0x05,
			reflect.TypeOf(msgs.AddSpanInitiateReq{}),
			reflect.TypeOf(msgs.AddSpanInitiateResp{}),
		},
		// certify span. again, the file must be transient.
		{
			0x06,
			reflect.TypeOf(msgs.AddSpanCertifyReq{}),
			reflect.TypeOf(msgs.AddSpanCertifyResp{}),
		},
		// makes a transient file current. requires the inode, the
		// parent dir, and the filename.
		{
			0x07,
			reflect.TypeOf(msgs.LinkFileReq{}),
			reflect.TypeOf(msgs.LinkFileResp{}),
		},
		// turns a current outgoing edge into a snapshot owning edge. requires parent directory
		// and file name
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
			reflect.TypeOf(msgs.RemoveOwnedSnapshotFileEdgeReq{}),
			reflect.TypeOf(msgs.RemoveOwnedSnapshotFileEdgeResp{}),
		},
		// UNSAFE OPERATIONS -- these can break invariants.
		// Creates a directory with a given parent and given inode id. Unsafe because
		// we can create directories with a certain parent while the paren't isn't
		// pointing at them (or isn't even a valid inode). We'd break the "no directory leaks"
		// invariant or the "null dir owner <-> not current" invariant.
		{
			0x80,
			reflect.TypeOf(msgs.CreateDirectoryINodeReq{}),
			reflect.TypeOf(msgs.CreateDirectoryINodeResp{}),
		},
		// This is needed to remove directories -- but it can break the invariants
		// between edges pointing to the dir and the owner.
		{
			0x81,
			reflect.TypeOf(msgs.SetDirectoryOwnerReq{}),
			reflect.TypeOf(msgs.SetDirectoryOwnerResp{}),
		},
		// These is generally needed when we need to move/create things cross-shard, but
		// is unsafe for various reasons:
		// * W must remember to unlock the edge, otherwise it'll be locked forever.
		// * We must make sure to not end up with multiple owners for the target.
		// TODO add comment about how creating an unlocked current edge is no good
		// if we want to retry things safely. We might create the edge without realizing
		// that we did (e.g. timeouts), and somebody might move it away in the meantime (with
		// some shard-local operation).
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
			reflect.TypeOf(msgs.RemoveDirectoryReq{}),
			reflect.TypeOf(msgs.RemoveDirectoryResp{}),
		},
		{
			0x04,
			reflect.TypeOf(msgs.RenameDirectoryReq{}),
			reflect.TypeOf(msgs.RenameDirectoryResp{}),
		},
	}

	extras := []reflect.Type{
		reflect.TypeOf(msgs.TransientFile{}),
		reflect.TypeOf(msgs.FetchedBlock{}),
		reflect.TypeOf(msgs.Edge{}),
		reflect.TypeOf(msgs.EdgeWithOwnership{}),
		reflect.TypeOf(msgs.FetchedSpan{}),
		reflect.TypeOf(msgs.BlockInfo{}),
		reflect.TypeOf(msgs.NewBlockInfo{}),
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

	cppOutFilename := fmt.Sprintf("%s/../../cpp/eggs_msgs.h", cwd)
	cppOutFile, err := os.Create(cppOutFilename)
	if err != nil {
		panic(err)
	}
	defer cppOutFile.Close()
	cppOutFile.Write(generateCpp(errors, shardReqResps, cdcReqResps, extras))

}
