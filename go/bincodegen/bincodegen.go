package main

import (
	"bytes"
	_ "embed"
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

func (cg *goCodegen) pstep(e string) {
	fmt.Fprintf(cg.pack, "%sif err := %s; err != nil {\n", cg.tabs, e)
	fmt.Fprintf(cg.pack, "%s\treturn err\n", cg.tabs)
	fmt.Fprintf(cg.pack, "%s}\n", cg.tabs)
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
	case reflect.Bool, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		assertNoTag(expr)
		cg.pstep(fmt.Sprintf("bincode.PackScalar(w, %s(%v))", strings.ToLower(t.Kind().String()), expr.fld))
		cg.ustep(fmt.Sprintf("bincode.UnpackScalar(r, (*%s)(&%v))", strings.ToLower(t.Kind().String()), expr.fld))
	case reflect.Struct:
		assertNoTag(expr)
		cg.pstep(fmt.Sprintf("%v.Pack(w)", expr.fld))
		cg.ustep(fmt.Sprintf("%v.Unpack(r)", expr.fld))
	case reflect.Slice, reflect.String:
		elem := sliceTypeElem(expr.typ)
		if elem.Kind() == reflect.Uint8 && t.Name() == "Blob" {
			cg.pstep(fmt.Sprintf("bincode.PackBlob(w, %v)", expr.fld))
			cg.ustep(fmt.Sprintf("bincode.UnpackBlob(r, &%v)", expr.fld))
		} else if elem.Kind() == reflect.Uint8 {
			cg.pstep(fmt.Sprintf("bincode.PackBytes(w, []byte(%v))", expr.fld))
			if expr.typ.Kind() == reflect.String {
				cg.ustep(fmt.Sprintf("bincode.UnpackString(r, &%v)", expr.fld))
			} else {
				cg.ustep(fmt.Sprintf("bincode.UnpackBytes(r, (*[]byte)(&%v))", expr.fld))
			}
		} else {
			lenVar := cg.lenVar()
			// handle length
			cg.pline(fmt.Sprintf("%s := len(%s)", lenVar, expr.fld))
			cg.pstep(fmt.Sprintf("bincode.PackLength(w, %s)", lenVar))
			cg.uline(fmt.Sprintf("var %s int", lenVar))
			cg.ustep(fmt.Sprintf("bincode.UnpackLength(r, &%s)", lenVar))
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
		cg.pstep(fmt.Sprintf("bincode.PackFixedBytes(w, %d, %v[:])", len, expr.fld))
		cg.ustep(fmt.Sprintf("bincode.UnpackFixedBytes(r, %d, %v[:])", len, expr.fld))
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

	cg.pline(fmt.Sprintf("func (v *%s) Pack(w io.Writer) error {", t.Name()))
	cg.uline(fmt.Sprintf("func (v *%s) Unpack(r io.Reader) error {", t.Name()))
	for i := 0; i < t.NumField(); i++ {
		fld := t.Field(i)
		cg.genInSlice(&subexpr{
			fld: fmt.Sprintf("v.%s", fld.Name),
			tag: parseTag(fld.Tag),
			typ: fld.Type,
		})
	}
	cg.pline("\treturn nil")
	cg.uline("\treturn nil")
	cg.pline("}\n")
	cg.uline("}\n")

	out.Write(cg.pack.Bytes())
	out.Write(cg.unpack.Bytes())
}

func generateGoReqResp(out io.Writer, rr reqRespType, enumType string, reqKindFun string, respKindFun string) {
	fmt.Fprintf(out, "func (v *%s) %s() %s {\n", rr.req.Name(), reqKindFun, enumType)
	fmt.Fprintf(out, "\treturn %s\n", reqRespEnum(rr))
	fmt.Fprintf(out, "}\n\n")
	generateGoSingle(out, rr.req)

	fmt.Fprintf(out, "func (v *%s) %s() %s {\n", rr.resp.Name(), respKindFun, enumType)
	fmt.Fprintf(out, "\treturn %s\n", reqRespEnum(rr))
	fmt.Fprintf(out, "}\n\n")
	generateGoSingle(out, rr.resp)
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

func generateGoMsgKind(out io.Writer, kindTypeName string, reqInterface string, respInterface string, mkName string, reqResps []reqRespType) {
	seenKinds := map[uint8]bool{}

	fmt.Fprintf(out, "func (k %s) String() string {\n", kindTypeName)
	fmt.Fprintf(out, "\tswitch k {\n")
	for _, reqResp := range reqResps {
		present := seenKinds[reqResp.kind]
		if present {
			panic(fmt.Errorf("duplicate kind %d for %s", reqResp.kind, kindTypeName))
		}
		fmt.Fprintf(out, "\tcase %v:\n", reqResp.kind)
		fmt.Fprintf(out, "\t\treturn \"%s\"\n", reqRespEnum(reqResp))
	}
	fmt.Fprintf(out, "\tdefault:\n")
	fmt.Fprintf(out, "\t\treturn fmt.Sprintf(\"%s(%%d)\", k)\n", kindTypeName)
	fmt.Fprintf(out, "\t}\n")
	fmt.Fprintf(out, "}\n\n")

	fmt.Fprintf(out, "\n")

	maxMessageKind := uint8(0)
	fmt.Fprintf(out, "const (\n")
	for _, reqResp := range reqResps {
		fmt.Fprintf(out, "\t%s %s = 0x%X\n", reqRespEnum(reqResp), kindTypeName, reqResp.kind)
		if reqResp.kind > maxMessageKind {
			maxMessageKind = reqResp.kind
		}
	}
	fmt.Fprintf(out, ")\n\n")

	fmt.Fprintf(out, "var All%s = [...]%s{\n", kindTypeName, kindTypeName)
	for _, reqResp := range reqResps {
		fmt.Fprintf(out, "\t%s,\n", reqRespEnum(reqResp))
	}
	fmt.Fprintf(out, "}\n\n")

	fmt.Fprintf(out, "const Max%s %s = %v\n\n", kindTypeName, kindTypeName, maxMessageKind)

	fmt.Fprintf(out, "func %s(k string) (%s, %s, error) {\n", mkName, reqInterface, respInterface)
	fmt.Fprintf(out, "\tswitch {\n")
	for _, reqResp := range reqResps {
		fmt.Fprintf(out, "\tcase k == %q:\n", reqRespEnum(reqResp))
		fmt.Fprintf(out, "\t\treturn &%v{}, &%v{}, nil\n", reqResp.req.Name(), reqResp.resp.Name())
	}
	fmt.Fprintf(out, "\tdefault:\n")
	fmt.Fprintf(out, "\t\treturn nil, nil, fmt.Errorf(\"bad kind string %%s\", k)\n")
	fmt.Fprintf(out, "\t}\n")
	fmt.Fprintf(out, "}\n\n")
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

//go:embed msgs_bincode.go.header
var goHeader string

func generateGo(errors []string, shardReqResps []reqRespType, cdcReqResps []reqRespType, shuckleReqResps []reqRespType, blocksReqResps []reqRespType, extras []reflect.Type) []byte {
	out := new(bytes.Buffer)

	out.Write([]byte(goHeader))

	generateGoErrorCodes(out, errors)

	generateGoMsgKind(out, "ShardMessageKind", "ShardRequest", "ShardResponse", "MkShardMessage", shardReqResps)
	generateGoMsgKind(out, "CDCMessageKind", "CDCRequest", "CDCResponse", "MkCDCMessage", cdcReqResps)
	generateGoMsgKind(out, "ShuckleMessageKind", "ShuckleRequest", "ShuckleResponse", "MkShuckleMessage", shuckleReqResps)
	generateGoMsgKind(out, "BlocksMessageKind", "BlocksRequest", "BlocksResponse", "MkBlocksMessage", blocksReqResps)

	for _, reqResp := range shardReqResps {
		generateGoReqResp(out, reqResp, "ShardMessageKind", "ShardRequestKind", "ShardResponseKind")
	}
	for _, reqResp := range cdcReqResps {
		generateGoReqResp(out, reqResp, "CDCMessageKind", "CDCRequestKind", "CDCResponseKind")
	}
	for _, typ := range extras {
		generateGoSingle(out, typ)
	}
	for _, reqResp := range shuckleReqResps {
		generateGoReqResp(out, reqResp, "ShuckleMessageKind", "ShuckleRequestKind", "ShuckleResponseKind")
	}
	for _, reqResp := range blocksReqResps {
		generateGoReqResp(out, reqResp, "BlocksMessageKind", "BlocksRequestKind", "BlocksResponseKind")
	}

	return out.Bytes()
}

func generateKmodMsgKind(hOut io.Writer, cOut io.Writer, what string, reqResps []reqRespType) {
	for _, reqResp := range reqResps {
		fmt.Fprintf(hOut, "#define EGGSFS_%s_%s 0x%X\n", what, reqRespEnum(reqResp), reqResp.kind)
	}
	fmt.Fprintf(hOut, "#define __print_eggsfs_%s_kind(k) __print_symbolic(k", strings.ToLower(what))
	for _, reqResp := range reqResps {
		fmt.Fprintf(hOut, ", { %d, %q }", reqResp.kind, reqRespEnum(reqResp))
	}
	fmt.Fprintf(hOut, ")\n")
	fmt.Fprintf(hOut, "const char* eggsfs_%s_kind_str(int kind);\n\n", strings.ToLower(what))

	fmt.Fprintf(cOut, "const char* eggsfs_%s_kind_str(int kind) {\n", strings.ToLower(what))
	fmt.Fprintf(cOut, "    switch (kind) {\n")
	for _, reqResp := range reqResps {
		fmt.Fprintf(cOut, "    case %d: return %q;\n", reqResp.kind, reqRespEnum(reqResp))
	}
	fmt.Fprintf(cOut, "    default: return \"UNKNOWN\";\n")
	fmt.Fprintf(cOut, "    }\n")
	fmt.Fprintf(cOut, "}\n\n")
}

func kmodFieldName(s string) string {
	re := regexp.MustCompile(`(.)([A-Z])`)
	return strings.ToLower(string(re.ReplaceAll([]byte(s), []byte("${1}_${2}"))))
}

func kmodStructName(typ reflect.Type) string {
	re := regexp.MustCompile(`(.)([A-Z])`)
	s := strings.ToLower(string(re.ReplaceAll([]byte(typ.Name()), []byte("${1}_${2}"))))
	return fmt.Sprintf("eggsfs_%s", s)
}

func kmodStaticSizeName(typ reflect.Type) string {
	re := regexp.MustCompile(`(.)([A-Z])`)
	s := strings.ToUpper(string(re.ReplaceAll([]byte(typ.Name()), []byte("${1}_${2}"))))
	return fmt.Sprintf("EGGSFS_%s_SIZE", s)
}

func kmodMaxSizeName(typ reflect.Type) string {
	re := regexp.MustCompile(`(.)([A-Z])`)
	s := strings.ToUpper(string(re.ReplaceAll([]byte(typ.Name()), []byte("${1}_${2}"))))
	return fmt.Sprintf("EGGSFS_%s_MAX_SIZE", s)
}

func generateKmodStructOpaque(w io.Writer, typ reflect.Type, suffix string) {
	if typ.Kind() != reflect.Struct {
		panic(fmt.Errorf("unexpected non-struct type %v", typ))
	}
	fmt.Fprintf(w, "struct %s_%s;\n", kmodStructName(typ), suffix)
}

func generateKmodStruct(w io.Writer, typ reflect.Type, fldName string, fld string) {
	if typ.Kind() != reflect.Struct {
		panic(fmt.Errorf("unexpected non-struct type %v", typ))
	}
	fmt.Fprintf(w, "struct %s_%s { %s; };\n", kmodStructName(typ), fldName, fld)
}

func generateKmodSize(staticSizes map[string]int, maxSizes map[string]int, w io.Writer, typ reflect.Type) {
	if typ.Kind() != reflect.Struct {
		panic(fmt.Errorf("unexpected non-struct type %v", typ))
	}
	updateSize := func(size *int, update int) {
		if update < 0 {
			*size = -1
		} else if *size >= 0 {
			*size += update
		}
	}
	staticSize := 0
	maxSize := 0
	for i := 0; i < typ.NumField(); i++ {
		fld := typ.Field(i)
		fldK := fld.Type.Kind()
		if fldK == reflect.Struct {
			fldStaticSize, found := staticSizes[kmodStaticSizeName(fld.Type)]
			if found {
				updateSize(&staticSize, fldStaticSize)
				updateSize(&maxSize, fldStaticSize)
			} else {
				updateSize(&staticSize, -1)
				fldMaxSize, found := staticSizes[kmodMaxSizeName(fld.Type)]
				if found {
					updateSize(&maxSize, fldMaxSize)
				} else {
					updateSize(&maxSize, -1)
				}
			}
		} else if fldK == reflect.Bool || fldK == reflect.Uint8 || fldK == reflect.Uint16 || fldK == reflect.Uint32 || fldK == reflect.Uint64 || fldK == reflect.Array {
			updateSize(&staticSize, int(fld.Type.Size()))
			updateSize(&maxSize, int(fld.Type.Size()))
		} else if fldK == reflect.Slice || fldK == reflect.String {
			updateSize(&staticSize, -1)
			elem := sliceTypeElem(fld.Type)
			if elem.Kind() == reflect.Uint8 {
				updateSize(&maxSize, 256) // len + body
			} else {
				updateSize(&maxSize, -1)
			}
		} else {
			panic(fmt.Errorf("unexpected kind %v", fldK))
		}
	}
	if staticSize >= 0 {
		sizeName := kmodStaticSizeName(typ)
		staticSizes[sizeName] = staticSize
		fmt.Fprintf(w, "#define %s %v\n", sizeName, staticSize)
	} else if maxSize >= 0 {
		sizeName := kmodMaxSizeName(typ)
		maxSizes[sizeName] = maxSize
		fmt.Fprintf(w, "#define %s %v\n", sizeName, maxSize)
	}
}

func kmodType(t reflect.Type) string {
	switch t.Kind() {
	case reflect.Uint8:
		return "u8"
	case reflect.Uint16:
		return "u16"
	case reflect.Uint32:
		return "u32"
	case reflect.Uint64:
		return "u64"
	case reflect.Bool:
		return "bool"
	case reflect.Struct:
		return fmt.Sprintf("%s*", kmodStructName(t))
	case reflect.Slice, reflect.String:
		elem := sliceTypeElem(t)
		if elem.Kind() == reflect.Uint8 {
			return "eggsfs_bytes*"
		} else {
			return kmodType(elem)
		}
	default:
		panic(fmt.Sprintf("unsupported type with kind %v", t.Kind()))
	}
}

func generateKmodGet(staticSizes map[string]int, h io.Writer, typ reflect.Type) {
	generateKmodStructOpaque(h, typ, "start")
	fmt.Fprintf(h, "#define %s_get_start(ctx, start) struct %s_start* start = NULL\n\n", kmodStructName(typ), kmodStructName(typ))
	prevType := fmt.Sprintf("struct %s_start*", kmodStructName(typ))
	for i := 0; i < typ.NumField(); i++ {
		fld := typ.Field(i)
		fldK := fld.Type.Kind()
		fldTyp := fld.Type
		endianness := "le"
		if fldK == reflect.Array {
			sz := fld.Type.Size()
			endianness = "be"
			if sz == 4 {
				fldK = reflect.Uint32
				fldTyp = reflect.TypeOf(uint32(0))
			} else if sz == 8 {
				fldK = reflect.Uint64
				fldTyp = reflect.TypeOf(uint64(0))
			} else {
				panic(fmt.Errorf("bad array size %v", fld.Type.Size()))
			}
		}
		fldName := kmodFieldName(fld.Name)
		fldKTyp := kmodType(fldTyp)
		if fldK == reflect.Struct {
			fmt.Fprintf(h, "#define %s_get_%s(ctx, prev, next) \\\n", kmodStructName(typ), fldName)
			fmt.Fprintf(h, "    { %s* __dummy __attribute__((unused)) = &(prev); }; \\\n", prevType)
			fmt.Fprintf(h, "    struct %s_start* next = NULL\n\n", kmodStructName(fldTyp))
			prevType = fmt.Sprintf("struct %s_end*", kmodStructName(fldTyp))
		} else if fldK == reflect.Slice || fldK == reflect.String {
			elemTyp := sliceTypeElem(fldTyp)
			if elemTyp.Kind() == reflect.Uint8 {
				generateKmodStruct(h, typ, fldName, "struct eggsfs_bincode_bytes str")
				nextType := fmt.Sprintf("struct %s_%s", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "static inline void _%s_get_%s(struct eggsfs_bincode_get_ctx* ctx, %s* prev, %s* next) {\n", kmodStructName(typ), fldName, prevType, nextType)
				fmt.Fprintf(h, "    if (likely(ctx->err == 0)) {\n")
				fmt.Fprintf(h, "        if (unlikely(ctx->end - ctx->buf < 1)) {\n")
				fmt.Fprintf(h, "            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;\n")
				fmt.Fprintf(h, "        } else {\n")
				fmt.Fprintf(h, "            next->str.len = *(u8*)(ctx->buf);\n")
				fmt.Fprintf(h, "            ctx->buf++;\n")
				fmt.Fprintf(h, "            if (unlikely(ctx->end - ctx->buf < next->str.len)) {\n")
				fmt.Fprintf(h, "                ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;\n")
				fmt.Fprintf(h, "            } else {\n")
				fmt.Fprintf(h, "                next->str.buf = ctx->buf;\n")
				fmt.Fprintf(h, "                ctx->buf += next->str.len;\n")
				fmt.Fprintf(h, "            }\n")
				fmt.Fprintf(h, "        }\n")
				fmt.Fprintf(h, "    }\n")
				fmt.Fprintf(h, "}\n")
				fmt.Fprintf(h, "#define %s_get_%s(ctx, prev, next) \\\n", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "    struct %s_%s next; \\\n", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "    _%s_get_%s(ctx, &(prev), &(next))\n\n", kmodStructName(typ), fldName)
				prevType = nextType
			} else {
				generateKmodStruct(h, typ, fldName, "u16 len")
				nextType := fmt.Sprintf("struct %s_%s", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "static inline void _%s_get_%s(struct eggsfs_bincode_get_ctx* ctx, %s* prev, %s* next) {\n", kmodStructName(typ), fldName, prevType, nextType)
				fmt.Fprintf(h, "    if (likely(ctx->err == 0)) {\n")
				fmt.Fprintf(h, "        if (unlikely(ctx->end - ctx->buf < 2)) {\n")
				fmt.Fprintf(h, "            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;\n")
				fmt.Fprintf(h, "        } else {\n")
				fmt.Fprintf(h, "            next->len = get_unaligned_le16(ctx->buf);\n")
				fmt.Fprintf(h, "            ctx->buf += 2;\n")
				fmt.Fprintf(h, "        }\n")
				fmt.Fprintf(h, "    } else {\n")
				fmt.Fprintf(h, "        next->len = 0;\n") // so that loops can work even in the case of errors
				fmt.Fprintf(h, "    }\n")
				fmt.Fprintf(h, "}\n")
				fmt.Fprintf(h, "#define %s_get_%s(ctx, prev, next) \\\n", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "    struct %s_%s next; \\\n", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "    _%s_get_%s(ctx, &(prev), &(next))\n\n", kmodStructName(typ), fldName)
				prevType = nextType
			}
		} else if fldK == reflect.Bool || fldK == reflect.Uint8 || fldK == reflect.Uint16 || fldK == reflect.Uint32 || fldK == reflect.Uint64 {
			generateKmodStruct(h, typ, fldName, fmt.Sprintf("%s x", fldKTyp))
			nextType := fmt.Sprintf("struct %s_%s", kmodStructName(typ), fldName)
			fmt.Fprintf(h, "static inline void _%s_get_%s(struct eggsfs_bincode_get_ctx* ctx, %s* prev, %s* next) {\n", kmodStructName(typ), fldName, prevType, nextType)
			fmt.Fprintf(h, "    if (likely(ctx->err == 0)) {\n")
			fmt.Fprintf(h, "        if (unlikely(ctx->end - ctx->buf < %d)) {\n", fldTyp.Size())
			fmt.Fprintf(h, "            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;\n")
			fmt.Fprintf(h, "        } else {\n")
			switch fldK {
			case reflect.Bool:
				fmt.Fprintf(h, "            next->x = *(bool*)(ctx->buf);\n")
			case reflect.Uint8:
				fmt.Fprintf(h, "            next->x = *(u8*)(ctx->buf);\n")
			case reflect.Uint16:
				fmt.Fprintf(h, "            next->x = get_unaligned_%s16(ctx->buf);\n", endianness)
			case reflect.Uint32:
				fmt.Fprintf(h, "            next->x = get_unaligned_%s32(ctx->buf);\n", endianness)
			case reflect.Uint64:
				fmt.Fprintf(h, "            next->x = get_unaligned_%s64(ctx->buf);\n", endianness)
			}
			fmt.Fprintf(h, "            ctx->buf += %d;\n", fldTyp.Size())
			fmt.Fprintf(h, "        }\n")
			fmt.Fprintf(h, "    }\n")
			fmt.Fprintf(h, "}\n")
			fmt.Fprintf(h, "#define %s_get_%s(ctx, prev, next) \\\n", kmodStructName(typ), fldName)
			fmt.Fprintf(h, "    struct %s_%s next; \\\n", kmodStructName(typ), fldName)
			fmt.Fprintf(h, "    _%s_get_%s(ctx, &(prev), &(next))\n\n", kmodStructName(typ), fldName)
			prevType = nextType
		} else {
			panic(fmt.Errorf("unexpected kind %v", fldK))
		}
	}
	generateKmodStructOpaque(h, typ, "end")
	fmt.Fprintf(h, "#define %s_get_end(ctx, prev, next) \\\n", kmodStructName(typ))
	fmt.Fprintf(h, "    { %s* __dummy __attribute__((unused)) = &(prev); }\\\n", prevType)
	fmt.Fprintf(h, "    struct %s_end* next = NULL\n\n", kmodStructName(typ))
	fmt.Fprintf(h, "static inline void %s_get_finish(struct eggsfs_bincode_get_ctx* ctx, struct %s_end* end) {\n", kmodStructName(typ), kmodStructName(typ))
	fmt.Fprintf(h, "    if (unlikely(ctx->buf != ctx->end)) {\n")
	fmt.Fprintf(h, "        ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;\n")
	fmt.Fprintf(h, "    }\n")
	fmt.Fprintf(h, "}\n\n")
}

func generateKmodPut(staticSizes map[string]int, h io.Writer, typ reflect.Type) {
	fmt.Fprintf(h, "#define %s_put_start(ctx, start) struct %s_start* start = NULL\n\n", kmodStructName(typ), kmodStructName(typ))
	prevType := fmt.Sprintf("struct %s_start*", kmodStructName(typ))
	for i := 0; i < typ.NumField(); i++ {
		fld := typ.Field(i)
		fldK := fld.Type.Kind()
		fldTyp := fld.Type
		endianness := "le"
		if fldK == reflect.Array {
			sz := fld.Type.Size()
			endianness = "be"
			if sz == 4 {
				fldK = reflect.Uint32
				fldTyp = reflect.TypeOf(uint32(0))
			} else if sz == 8 {
				fldK = reflect.Uint64
				fldTyp = reflect.TypeOf(uint64(0))
			} else {
				panic(fmt.Errorf("bad array size %v", fld.Type.Size()))
			}
		}
		fldName := kmodFieldName(fld.Name)
		fldKTyp := kmodType(fldTyp)
		if fldK == reflect.Struct {
			// fmt.Fprintf(h, "#define %s_get_%s(ctx, prev, next) \\\n", kmodStructName(typ), fldName)
			// fmt.Fprintf(h, "    { %s* __dummy __attribute__((unused)) = &(prev); }; \\\n", prevType)
			// fmt.Fprintf(h, "    struct %s_start* next = NULL\n\n", kmodStructName(fldTyp))
			// prevType = fmt.Sprintf("struct %s_end*", kmodStructName(fldTyp))
		} else if fldK == reflect.Slice || fldK == reflect.String {
			elemTyp := sliceTypeElem(fldTyp)
			nextType := fmt.Sprintf("struct %s_%s", kmodStructName(typ), fldName)
			if elemTyp.Kind() == reflect.Uint8 {
				fmt.Fprintf(h, "static inline void _%s_put_%s(struct eggsfs_bincode_put_ctx* ctx, %s* prev, %s* next, const char* str, int str_len) {\n", kmodStructName(typ), fldName, prevType, nextType)
				fmt.Fprintf(h, "    next = NULL;\n")
				fmt.Fprintf(h, "    BUG_ON(str_len < 0 || str_len > 255);\n")
				fmt.Fprintf(h, "    BUG_ON(ctx->end - ctx->cursor < (1 + str_len));\n")
				fmt.Fprintf(h, "    *(u8*)(ctx->cursor) = str_len;\n")
				fmt.Fprintf(h, "    memcpy(ctx->cursor + 1, str, str_len);\n")
				fmt.Fprintf(h, "    ctx->cursor += 1 + str_len;\n")
				fmt.Fprintf(h, "}\n")
				fmt.Fprintf(h, "#define %s_put_%s(ctx, prev, next, str, str_len) \\\n", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "    struct %s_%s next; \\\n", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "    _%s_put_%s(ctx, &(prev), &(next), str, str_len)\n\n", kmodStructName(typ), fldName)
			} else {
				fmt.Fprintf(h, "static inline void _%s_put_%s(struct eggsfs_bincode_put_ctx* ctx, %s* prev, %s* next, int len) {\n", kmodStructName(typ), fldName, prevType, nextType)
				fmt.Fprintf(h, "    next = NULL;\n")
				fmt.Fprintf(h, "    BUG_ON(len < 0 || len >= 1<<16);\n")
				fmt.Fprintf(h, "    BUG_ON(ctx->end - ctx->cursor < 2);\n")
				fmt.Fprintf(h, "    put_unaligned_le16(len, ctx->cursor);\n")
				fmt.Fprintf(h, "    ctx->cursor += 2;\n")
				fmt.Fprintf(h, "}\n")
				fmt.Fprintf(h, "#define %s_put_%s(ctx, prev, next, len) \\\n", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "    struct %s_%s next; \\\n", kmodStructName(typ), fldName)
				fmt.Fprintf(h, "    _%s_put_%s(ctx, &(prev), &(next), len)\n\n", kmodStructName(typ), fldName)
			}
			prevType = nextType
		} else if fldK == reflect.Bool || fldK == reflect.Uint8 || fldK == reflect.Uint16 || fldK == reflect.Uint32 || fldK == reflect.Uint64 {
			nextType := fmt.Sprintf("struct %s_%s", kmodStructName(typ), fldName)
			fmt.Fprintf(h, "static inline void _%s_put_%s(struct eggsfs_bincode_put_ctx* ctx, %s* prev, %s* next, %s x) {\n", kmodStructName(typ), fldName, prevType, nextType, fldKTyp)
			fmt.Fprintf(h, "    next = NULL;\n")
			fmt.Fprintf(h, "    BUG_ON(ctx->end - ctx->cursor < %d);\n", fldTyp.Size())
			switch fldK {
			case reflect.Bool:
				fmt.Fprintf(h, "    *(bool*)(ctx->cursor) = x;\n")
			case reflect.Uint8:
				fmt.Fprintf(h, "    *(u8*)(ctx->cursor) = x;\n")
			case reflect.Uint16:
				fmt.Fprintf(h, "    put_unaligned_%s16(x, ctx->cursor);\n", endianness)
			case reflect.Uint32:
				fmt.Fprintf(h, "    put_unaligned_%s32(x, ctx->cursor);\n", endianness)
			case reflect.Uint64:
				fmt.Fprintf(h, "    put_unaligned_%s64(x, ctx->cursor);\n", endianness)
			}
			fmt.Fprintf(h, "    ctx->cursor += %d;\n", fldTyp.Size())
			fmt.Fprintf(h, "}\n")
			fmt.Fprintf(h, "#define %s_put_%s(ctx, prev, next, x) \\\n", kmodStructName(typ), fldName)
			fmt.Fprintf(h, "    struct %s_%s next; \\\n", kmodStructName(typ), fldName)
			fmt.Fprintf(h, "    _%s_put_%s(ctx, &(prev), &(next), x)\n\n", kmodStructName(typ), fldName)
			prevType = nextType
		} else {
			panic(fmt.Errorf("unexpected kind %v", fldK))
		}
	}
	fmt.Fprintf(h, "#define %s_put_end(ctx, prev, next) \\\n", kmodStructName(typ))
	fmt.Fprintf(h, "    { %s* __dummy __attribute__((unused)) = &(prev); }\\\n", prevType)
	fmt.Fprintf(h, "    struct %s_end* next __attribute__((unused)) = NULL\n\n", kmodStructName(typ))
}

func generateKmod(errors []string, shardReqResps []reqRespType, cdcReqResps []reqRespType, shuckleReqResps []reqRespType, blocksReqResps []reqRespType, extras []reflect.Type) ([]byte, []byte) {
	hOut := new(bytes.Buffer)
	cOut := new(bytes.Buffer)

	fmt.Fprintln(hOut, "// Automatically generated with go run bincodegen.")
	fmt.Fprintln(hOut, "// Run `go generate ./...` from the go/ directory to regenerate it.")
	fmt.Fprintln(hOut)

	for i, err := range errors {
		fmt.Fprintf(hOut, "#define EGGSFS_ERR_%s %d\n", err, errCodeOffset+i)
	}
	fmt.Fprintf(hOut, "\n")
	fmt.Fprintf(hOut, "#define __print_eggsfs_err(i) __print_symbolic(i")
	for i, err := range errors {
		fmt.Fprintf(hOut, ", { %d, %q }", errCodeOffset+i, err)
	}
	fmt.Fprintf(hOut, ")\n")
	fmt.Fprintf(hOut, "const char* eggsfs_err_str(int err);\n\n")

	fmt.Fprintf(cOut, "const char* eggsfs_err_str(int err) {\n")
	fmt.Fprintf(cOut, "    switch (err) {\n")
	for i, err := range errors {
		fmt.Fprintf(cOut, "    case %d: return %q;\n", errCodeOffset+i, err)
	}
	fmt.Fprintf(cOut, "    default: return \"UNKNOWN\";\n")
	fmt.Fprintf(cOut, "    }\n")
	fmt.Fprintf(cOut, "}\n\n")

	generateKmodMsgKind(hOut, cOut, "SHARD", shardReqResps)
	generateKmodMsgKind(hOut, cOut, "CDC", cdcReqResps)
	generateKmodMsgKind(hOut, cOut, "SHUCKLE", shuckleReqResps)
	generateKmodMsgKind(hOut, cOut, "BLOCKS", blocksReqResps)

	fmt.Fprintln(hOut)

	staticSizes := make(map[string]int)
	maxSizes := make(map[string]int)
	for _, typ := range extras {
		generateKmodSize(staticSizes, maxSizes, hOut, typ)
		generateKmodGet(staticSizes, hOut, typ)
		generateKmodPut(staticSizes, hOut, typ)
	}

	generateReqResps := func(reqResps []reqRespType) {
		for _, reqResp := range reqResps {
			generateKmodSize(staticSizes, maxSizes, hOut, reqResp.req)
			generateKmodGet(staticSizes, hOut, reqResp.req)
			generateKmodPut(staticSizes, hOut, reqResp.req)
			generateKmodSize(staticSizes, maxSizes, hOut, reqResp.resp)
			generateKmodGet(staticSizes, hOut, reqResp.resp)
			generateKmodPut(staticSizes, hOut, reqResp.resp)
		}
	}

	generateReqResps(shardReqResps)
	generateReqResps(cdcReqResps)
	generateReqResps(shuckleReqResps)
	generateReqResps(blocksReqResps)

	return hOut.Bytes(), cOut.Bytes()
}

func cppType(t reflect.Type) string {
	if t.Name() == "InodeId" || t.Name() == "InodeIdExtra" || t.Name() == "Parity" || t.Name() == "EggsTime" || t.Name() == "ShardId" || t.Name() == "CDCMessageKind" || t.Name() == "Crc" || t.Name() == "BlockServiceId" {
		return t.Name()
	}
	if t.Name() == "Blob" {
		return "BincodeList<uint8_t>"
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
	if k == reflect.Struct || expr.typ.Name() == "InodeId" || expr.typ.Name() == "InodeIdExtra" || expr.typ.Name() == "Parity" || expr.typ.Name() == "EggsTime" || expr.typ.Name() == "ShardId" || expr.typ.Name() == "Crc" || expr.typ.Name() == "BlockServiceId" {
		cg.pline(fmt.Sprintf("%s.pack(buf)", expr.fld))
		cg.uline(fmt.Sprintf("%s.unpack(buf)", expr.fld))
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
		if elem.Kind() == reflect.Uint8 && expr.typ.Name() != "Blob" {
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
	if k == reflect.Array {
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
	fmt.Fprintf(hpp, "    %s() { clear(); }\n", t.Name())

	fmt.Fprintf(hpp, "    size_t packedSize() const {\n")
	fmt.Fprintf(hpp, "        size_t _size = 0;\n")
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
		} else if fld.Type.Kind() == reflect.String {
			fmt.Fprintf(cpp, " << \"%s=\" << GoLangQuotedStringFmt(x.%s.data(), x.%s.size())", fld.Name, cppFieldName(fld), cppFieldName(fld))
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

	fmt.Fprintf(hpp, "const std::vector<%sMessageKind> all%sMessageKind {\n", name, name)
	max := int(0)
	for _, reqResp := range reqResps {
		fmt.Fprintf(hpp, "    %sMessageKind::%s,\n", name, reqRespEnum(reqResp))
		if int(reqResp.kind) > max {
			max = int(reqResp.kind)
		}
	}
	fmt.Fprintf(hpp, "};\n\n")

	fmt.Fprintf(hpp, "constexpr int max%sMessageKind = %d;\n\n", name, max)

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
	fmt.Fprintf(hpp, "    size_t packedSize() const;\n")
	fmt.Fprintf(hpp, "    void pack(BincodeBuf& buf) const;\n")
	fmt.Fprintf(hpp, "    void unpack(BincodeBuf& buf, %s kind);\n", kindTypeName)
	fmt.Fprintf(hpp, "};\n\n")

	fmt.Fprintf(hpp, "std::ostream& operator<<(std::ostream& out, const %s& x);\n\n", name)

	fmt.Fprintf(cpp, "size_t %s::packedSize() const {\n", name)
	fmt.Fprintf(cpp, "    switch (_kind) {\n")
	for i, typ := range types {
		fmt.Fprintf(cpp, "    case %s::%s:\n", kindTypeName, typ.enum)
		fmt.Fprintf(cpp, "        return std::get<%d>(_data).packedSize();\n", i)
	}
	fmt.Fprintf(cpp, "    default:\n")
	fmt.Fprintf(cpp, "        throw EGGS_EXCEPTION(\"bad %s kind %%s\", _kind);\n", kindTypeName)
	fmt.Fprintf(cpp, "    }\n")
	fmt.Fprintf(cpp, "}\n\n")
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

//go:embed FetchedSpan.hpp
var fetchedSpanCpp string

func generateCpp(errors []string, shardReqResps []reqRespType, cdcReqResps []reqRespType, shuckleReqResps []reqRespType, blocksReqResps []reqRespType, extras []reflect.Type) ([]byte, []byte) {
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

	generateCppKind(hppOut, cppOut, "Shard", shardReqResps)
	generateCppKind(hppOut, cppOut, "CDC", cdcReqResps)
	generateCppKind(hppOut, cppOut, "Shuckle", shuckleReqResps)
	generateCppKind(hppOut, cppOut, "Blocks", blocksReqResps)

	for _, typ := range extras {
		generateCppSingle(hppOut, cppOut, typ)
		if typ == reflect.TypeOf(msgs.FetchedBlocksSpan{}) {
			// inject the hand-written definition for FetchedSpan
			hppOut.Write([]byte(fetchedSpanCpp))
		}
	}
	for _, reqResp := range shardReqResps {
		generateCppSingle(hppOut, cppOut, reqResp.req)
		generateCppSingle(hppOut, cppOut, reqResp.resp)
	}
	for _, reqResp := range cdcReqResps {
		generateCppSingle(hppOut, cppOut, reqResp.req)
		generateCppSingle(hppOut, cppOut, reqResp.resp)
	}
	for _, reqResp := range shuckleReqResps {
		generateCppSingle(hppOut, cppOut, reqResp.req)
		generateCppSingle(hppOut, cppOut, reqResp.resp)
	}
	for _, reqResp := range blocksReqResps {
		generateCppSingle(hppOut, cppOut, reqResp.req)
		generateCppSingle(hppOut, cppOut, reqResp.resp)
	}

	generateCppReqResp(hppOut, cppOut, "Shard", shardReqResps)
	generateCppReqResp(hppOut, cppOut, "CDC", cdcReqResps)
	generateCppReqResp(hppOut, cppOut, "Shuckle", shuckleReqResps)

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
			reflect.TypeOf(msgs.SameShardHardFileUnlinkEntry{}),
			reflect.TypeOf(msgs.RemoveSpanInitiateEntry{}),
			reflect.TypeOf(msgs.UpdateBlockServicesEntry{}),
			reflect.TypeOf(msgs.AddSpanInitiateEntry{}),
			reflect.TypeOf(msgs.AddSpanCertifyEntry{}),
			reflect.TypeOf(msgs.AddInlineSpanEntry{}),
			reflect.TypeOf(msgs.MakeFileTransientEntry{}),
			reflect.TypeOf(msgs.RemoveSpanCertifyEntry{}),
			reflect.TypeOf(msgs.RemoveOwnedSnapshotFileEdgeEntry{}),
			reflect.TypeOf(msgs.SwapBlocksEntry{}),
			reflect.TypeOf(msgs.ExpireTransientFileEntry{}),
			reflect.TypeOf(msgs.MoveSpanEntry{}),
			reflect.TypeOf(msgs.SetTimeEntry{}),
		},
	)

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
		"EDGE_NOT_FOUND",
		"EDGE_IS_LOCKED",
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
		"MTIME_IS_TOO_RECENT",
		"MISMATCHING_TARGET",
		"MISMATCHING_OWNER",
		"MISMATCHING_CREATION_TIME",
		"DIRECTORY_NOT_EMPTY",
		"FILE_IS_TRANSIENT",
		"OLD_DIRECTORY_NOT_FOUND",
		"NEW_DIRECTORY_NOT_FOUND",
		"LOOP_IN_DIRECTORY_RENAME",
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
		"DEADLINE_NOT_PASSED",
		"SAME_SOURCE_AND_DESTINATION",
		"SAME_DIRECTORIES",
		"SAME_SHARD",
		"BAD_PROTOCOL_VERSION",
		"BAD_CERTIFICATE",
		"BLOCK_TOO_RECENT_FOR_DELETION",
		"BLOCK_FETCH_OUT_OF_BOUNDS",
		"BAD_BLOCK_CRC",
		"BLOCK_TOO_BIG",
		"BLOCK_NOT_FOUND",
		"CANNOT_UNSET_DECOMMISSIONED",
		"CANNOT_REGISTER_DECOMMISSIONED",
	}

	kernelShardReqResps := []reqRespType{
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
			0x04,
			reflect.TypeOf(msgs.StatDirectoryReq{}),
			reflect.TypeOf(msgs.StatDirectoryResp{}),
		},
		{
			0x05,
			reflect.TypeOf(msgs.ReadDirReq{}),
			reflect.TypeOf(msgs.ReadDirResp{}),
		},
		{
			0x06,
			reflect.TypeOf(msgs.ConstructFileReq{}),
			reflect.TypeOf(msgs.ConstructFileResp{}),
		},
		{
			0x07,
			reflect.TypeOf(msgs.AddSpanInitiateReq{}),
			reflect.TypeOf(msgs.AddSpanInitiateResp{}),
		},
		{
			0x08,
			reflect.TypeOf(msgs.AddSpanCertifyReq{}),
			reflect.TypeOf(msgs.AddSpanCertifyResp{}),
		},
		{
			0x09,
			reflect.TypeOf(msgs.LinkFileReq{}),
			reflect.TypeOf(msgs.LinkFileResp{}),
		},
		{
			0x0A,
			reflect.TypeOf(msgs.SoftUnlinkFileReq{}),
			reflect.TypeOf(msgs.SoftUnlinkFileResp{}),
		},
		{
			0x0B,
			reflect.TypeOf(msgs.FileSpansReq{}),
			reflect.TypeOf(msgs.FileSpansResp{}),
		},
		{
			0x0C,
			reflect.TypeOf(msgs.SameDirectoryRenameReq{}),
			reflect.TypeOf(msgs.SameDirectoryRenameResp{}),
		},
		{
			0x10,
			reflect.TypeOf(msgs.AddInlineSpanReq{}),
			reflect.TypeOf(msgs.AddInlineSpanResp{}),
		},
		{
			0x11,
			reflect.TypeOf(msgs.SetTimeReq{}),
			reflect.TypeOf(msgs.SetTimeResp{}),
		},
		// this was marked as a "private" operation, but we now use it in
		// the client (to check deleted edges)
		{
			0x73,
			reflect.TypeOf(msgs.FullReadDirReq{}),
			reflect.TypeOf(msgs.FullReadDirResp{}),
		},
		// this is also "private" but we use it to statsh away broken dirty
		// spans
		{
			0x7B,
			reflect.TypeOf(msgs.MoveSpanReq{}),
			reflect.TypeOf(msgs.MoveSpanResp{}),
		},
		// again, this is private, but we plan to use it to restart writing empty
		// files.
		{
			0x74,
			reflect.TypeOf(msgs.RemoveNonOwnedEdgeReq{}),
			reflect.TypeOf(msgs.RemoveNonOwnedEdgeResp{}),
		},
		{
			0x75,
			reflect.TypeOf(msgs.SameShardHardFileUnlinkReq{}),
			reflect.TypeOf(msgs.SameShardHardFileUnlinkResp{}),
		},
	}

	shardReqResps := append(kernelShardReqResps, []reqRespType{
		{
			0x03,
			reflect.TypeOf(msgs.StatTransientFileReq{}),
			reflect.TypeOf(msgs.StatTransientFileResp{}),
		},
		{
			0x0D,
			reflect.TypeOf(msgs.SetDirectoryInfoReq{}),
			reflect.TypeOf(msgs.SetDirectoryInfoResp{}),
		},
		{
			0x0F,
			reflect.TypeOf(msgs.ExpireTransientFileReq{}),
			reflect.TypeOf(msgs.ExpireTransientFileResp{}),
		},
		// PRIVATE OPERATIONS -- These are safe operations, but we don't want the FS client itself
		// to perform them. TODO make privileged?
		{
			0x70,
			reflect.TypeOf(msgs.VisitDirectoriesReq{}),
			reflect.TypeOf(msgs.VisitDirectoriesResp{}),
		},
		{
			0x71,
			reflect.TypeOf(msgs.VisitFilesReq{}),
			reflect.TypeOf(msgs.VisitFilesResp{}),
		},
		{
			0x72,
			reflect.TypeOf(msgs.VisitTransientFilesReq{}),
			reflect.TypeOf(msgs.VisitTransientFilesResp{}),
		},
		{
			0x76,
			reflect.TypeOf(msgs.RemoveSpanInitiateReq{}),
			reflect.TypeOf(msgs.RemoveSpanInitiateResp{}),
		},
		{
			0x77,
			reflect.TypeOf(msgs.RemoveSpanCertifyReq{}),
			reflect.TypeOf(msgs.RemoveSpanCertifyResp{}),
		},
		{
			0x78,
			reflect.TypeOf(msgs.SwapBlocksReq{}),
			reflect.TypeOf(msgs.SwapBlocksResp{}),
		},
		{
			0x79,
			reflect.TypeOf(msgs.BlockServiceFilesReq{}),
			reflect.TypeOf(msgs.BlockServiceFilesResp{}),
		},
		{
			0x7A,
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
	}...)

	kernelCdcReqResps := []reqRespType{
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
	}

	cdcReqResps := append(kernelCdcReqResps, []reqRespType{
		{
			0x05,
			reflect.TypeOf(msgs.HardUnlinkDirectoryReq{}),
			reflect.TypeOf(msgs.HardUnlinkDirectoryResp{}),
		},
		{
			0x06,
			reflect.TypeOf(msgs.CrossShardHardUnlinkFileReq{}),
			reflect.TypeOf(msgs.CrossShardHardUnlinkFileResp{}),
		},
	}...)

	kernelShuckleReqResps := []reqRespType{
		{
			0x03,
			reflect.TypeOf(msgs.ShardsReq{}),
			reflect.TypeOf(msgs.ShardsResp{}),
		},
		{
			0x07,
			reflect.TypeOf(msgs.CdcReq{}),
			reflect.TypeOf(msgs.CdcResp{}),
		},
		{
			0x08,
			reflect.TypeOf(msgs.InfoReq{}),
			reflect.TypeOf(msgs.InfoResp{}),
		},
	}

	shuckleReqResps := append(kernelShuckleReqResps, []reqRespType{
		{
			0x02,
			reflect.TypeOf(msgs.RegisterBlockServicesReq{}),
			reflect.TypeOf(msgs.RegisterBlockServicesResp{}),
		},
		{
			0x04,
			reflect.TypeOf(msgs.RegisterShardReq{}),
			reflect.TypeOf(msgs.RegisterShardResp{}),
		},
		{
			0x05,
			reflect.TypeOf(msgs.AllBlockServicesReq{}),
			reflect.TypeOf(msgs.AllBlockServicesResp{}),
		},
		{
			0x06,
			reflect.TypeOf(msgs.RegisterCdcReq{}),
			reflect.TypeOf(msgs.RegisterCdcResp{}),
		},
		{
			0x09,
			reflect.TypeOf(msgs.SetBlockServiceFlagsReq{}),
			reflect.TypeOf(msgs.SetBlockServiceFlagsResp{}),
		},
		{
			0x0A,
			reflect.TypeOf(msgs.BlockServiceReq{}),
			reflect.TypeOf(msgs.BlockServiceResp{}),
		},
		{
			0x0B,
			reflect.TypeOf(msgs.InsertStatsReq{}),
			reflect.TypeOf(msgs.InsertStatsResp{}),
		},
		{
			0x0C,
			reflect.TypeOf(msgs.ShardReq{}),
			reflect.TypeOf(msgs.ShardResp{}),
		},
		{
			0x0D,
			reflect.TypeOf(msgs.GetStatsReq{}),
			reflect.TypeOf(msgs.GetStatsResp{}),
		},
	}...)

	kernelBlocksReqResps := []reqRespType{
		{
			0x02,
			reflect.TypeOf(msgs.FetchBlockReq{}),
			reflect.TypeOf(msgs.FetchBlockResp{}),
		},
		{
			0x03,
			reflect.TypeOf(msgs.WriteBlockReq{}),
			reflect.TypeOf(msgs.WriteBlockResp{}),
		},
	}

	blocksReqResps := append(kernelBlocksReqResps, []reqRespType{
		{
			0x01,
			reflect.TypeOf(msgs.EraseBlockReq{}),
			reflect.TypeOf(msgs.EraseBlockResp{}),
		},
		{
			0x05,
			reflect.TypeOf(msgs.TestWriteReq{}),
			reflect.TypeOf(msgs.TestWriteResp{}),
		},
	}...)

	kernelExtras := []reflect.Type{
		reflect.TypeOf(msgs.DirectoryInfoEntry{}),
		reflect.TypeOf(msgs.DirectoryInfo{}),
		reflect.TypeOf(msgs.CurrentEdge{}),
		reflect.TypeOf(msgs.BlockInfo{}),
		reflect.TypeOf(msgs.BlockProof{}),
		reflect.TypeOf(msgs.BlockService{}),
		reflect.TypeOf(msgs.ShardInfo{}),
		reflect.TypeOf(msgs.BlockPolicyEntry{}),
		reflect.TypeOf(msgs.SpanPolicyEntry{}),
		reflect.TypeOf(msgs.StripePolicy{}),
		reflect.TypeOf(msgs.FetchedBlock{}),
		reflect.TypeOf(msgs.FetchedSpanHeader{}),
		reflect.TypeOf(msgs.FetchedInlineSpan{}),
		reflect.TypeOf(msgs.FetchedBlocksSpan{}),
		reflect.TypeOf(msgs.BlacklistEntry{}),
		reflect.TypeOf(msgs.Edge{}),
		reflect.TypeOf(msgs.FullReadDirCursor{}),
	}

	extras := append([]reflect.Type{reflect.TypeOf(msgs.FailureDomain{})}, append(kernelExtras, []reflect.Type{
		reflect.TypeOf(msgs.TransientFile{}),
		reflect.TypeOf(msgs.EntryNewBlockInfo{}),
		reflect.TypeOf(msgs.BlockServiceInfo{}),
		reflect.TypeOf(msgs.RegisterShardInfo{}),
		reflect.TypeOf(msgs.SpanPolicy{}),
		reflect.TypeOf(msgs.BlockPolicy{}),
		reflect.TypeOf(msgs.SnapshotPolicy{}),
		reflect.TypeOf(msgs.Stat{}),
	}...)...)

	goCode := generateGo(errors, shardReqResps, cdcReqResps, shuckleReqResps, blocksReqResps, extras)
	goOutFileName := fmt.Sprintf("%s/msgs_bincode.go", cwd)
	goOutFile, err := os.Create(goOutFileName)
	if err != nil {
		panic(err)
	}
	defer goOutFile.Close()
	goOutFile.Write(goCode)

	kmodHOutFilename := fmt.Sprintf("%s/../../kmod/bincodegen.h", cwd)
	kmodHOutFile, err := os.Create(kmodHOutFilename)
	if err != nil {
		panic(err)
	}
	defer kmodHOutFile.Close()
	kmodCOutFilename := fmt.Sprintf("%s/../../kmod/bincodegen.c", cwd)
	kmodCOutFile, err := os.Create(kmodCOutFilename)
	if err != nil {
		panic(err)
	}
	defer kmodHOutFile.Close()
	kmodHBytes, kmodCBytes := generateKmod(errors, kernelShardReqResps, kernelCdcReqResps, kernelShuckleReqResps, kernelBlocksReqResps, kernelExtras)
	kmodHOutFile.Write(kmodHBytes)
	kmodCOutFile.Write(kmodCBytes)

	hppOutFilename := fmt.Sprintf("%s/../../cpp/core/MsgsGen.hpp", cwd)
	hppOutFile, err := os.Create(hppOutFilename)
	if err != nil {
		panic(err)
	}
	defer hppOutFile.Close()
	cppOutFilename := fmt.Sprintf("%s/../../cpp/core/MsgsGen.cpp", cwd)
	cppOutFile, err := os.Create(cppOutFilename)
	if err != nil {
		panic(err)
	}
	defer cppOutFile.Close()
	hppBytes, cppBytes := generateCpp(errors, shardReqResps, cdcReqResps, shuckleReqResps, blocksReqResps, extras)
	hppOutFile.Write(hppBytes)
	cppOutFile.Write(cppBytes)

}
