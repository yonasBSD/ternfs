package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"reflect"
	"regexp"
	"strconv"
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

type codegen struct {
	pack   *bytes.Buffer
	unpack *bytes.Buffer
	tabs   []byte
	fresh  *int
}

// pack line
func (cg *codegen) pline(e string) {
	fmt.Fprintf(cg.pack, "%s%s\n", cg.tabs, e)
}

// unpack line
func (cg *codegen) uline(e string) {
	fmt.Fprintf(cg.unpack, "%s%s\n", cg.tabs, e)
}

// unpack step, with error checking
func (cg *codegen) ustep(e string) {
	fmt.Fprintf(cg.unpack, "%sif err := %s; err != nil {\n", cg.tabs, e)
	fmt.Fprintf(cg.unpack, "%s\treturn err\n", cg.tabs)
	fmt.Fprintf(cg.unpack, "%s}\n", cg.tabs)
}

// lack of pointer is intentional: we don't want to destructively
// update the tabs.
func (cg codegen) genIndent(expr *subexpr) {
	cg.tabs = cg.tabs[:len(cg.tabs)+1]
	cg.gen(expr)
}

func (cg *codegen) lenVar() string {
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

func (cg *codegen) gen(expr *subexpr) {
	t := expr.typ
	switch t.Kind() {
	case reflect.Uint8:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU8(%v)", expr.expr))
		cg.ustep(fmt.Sprintf("buf.UnpackU8(&%s)", expr.expr))
	case reflect.Uint16:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU16(%v)", expr.expr))
		cg.ustep(fmt.Sprintf("buf.UnpackU16(&%s)", expr.expr))
	case reflect.Uint32:
		assertNoTag(expr)
		cg.pline(fmt.Sprintf("buf.PackU32(%v)", expr.expr))
		cg.ustep(fmt.Sprintf("buf.UnpackU32(&%s)", expr.expr))
	case reflect.Uint64:
		if expr.tag == "varint" {
			cg.pline(fmt.Sprintf("buf.PackVarU61(%v)", expr.expr))
			cg.ustep(fmt.Sprintf("buf.UnpackVarU61(&%s)", expr.expr))
		} else {
			assertExpectedTag(expr, `tag bincode:"varint" or no tag`)
			cg.pline(fmt.Sprintf("buf.PackU64(%v)", expr.expr))
			cg.ustep(fmt.Sprintf("buf.UnpackU64(&%s)", expr.expr))
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
				cg.pline(fmt.Sprintf("buf.PackBytes(%v)", expr.expr))
				cg.ustep(fmt.Sprintf("buf.UnpackBytes(&%v)", expr.expr))
			} else {
				cg.pline(fmt.Sprintf("buf.PackFixedBytes(%d, %v)", len, expr.expr))
				cg.ustep(fmt.Sprintf("buf.UnpackFixedBytes(%d, &%v)", len, expr.expr))
			}
		} else {
			lenVar := cg.lenVar()
			// handle length
			cg.pline(fmt.Sprintf("%s := len(%s)", lenVar, expr.expr))
			cg.pline(fmt.Sprintf("buf.PackLength(%s)", lenVar))
			cg.uline(fmt.Sprintf("var %s int", lenVar))
			cg.ustep(fmt.Sprintf("buf.UnpackLength(&%s)", lenVar))
			// handle body
			cg.uline(fmt.Sprintf("%s = make([]%v, %s)", expr.expr, elem.Name(), lenVar))
			loop := fmt.Sprintf("for i := 0; i < %s; i++ {", lenVar)
			cg.pline(loop)
			cg.uline(loop)
			cg.genIndent(&subexpr{
				typ:  elem,
				expr: fmt.Sprintf("%s[i]", expr.expr),
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

func generate(out io.Writer, t reflect.Type) {
	if t.Kind() != reflect.Struct {
		panic(fmt.Sprintf("type %v is not a struct", t))
	}

	tabs := make([]byte, 100)
	for i := 0; i < len(tabs); i++ {
		tabs[i] = '\t'
	}
	fresh := 0
	cg := codegen{
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

func main() {
	cwd, err := os.Getwd()
	if err != nil {
		panic(err)
	}
	outFileName := fmt.Sprintf("%s/msgs_bincode.go", cwd)
	outFile, err := os.Create(outFileName)
	if err != nil {
		panic(err)
	}
	defer func() {
		outFile.Close()
		// Let's not leave a possibly broken file lying around
		if err := recover(); err != nil {
			fmt.Printf("generation failed, will remove up %v\n", outFileName)
			os.Remove(outFileName) // ignore errors -- we're recovering anyway
			panic(err)
		}
	}()

	types := []reflect.Type{
		reflect.TypeOf(msgs.VisitInodesReq{}),
		reflect.TypeOf(msgs.VisitInodesResp{}),
		reflect.TypeOf(msgs.TransientFile{}),
		reflect.TypeOf(msgs.VisitTransientFilesReq{}),
		reflect.TypeOf(msgs.VisitTransientFilesResp{}),
		reflect.TypeOf(msgs.FileSpansReq{}),
		reflect.TypeOf(msgs.FetchedBlock{}),
		reflect.TypeOf(msgs.FetchedSpanHeader{}),
		reflect.TypeOf(msgs.FileSpansResp{}),
	}

	fmt.Fprintln(outFile, "// Automatically generated with go run bincodegen.")
	fmt.Fprintln(outFile, "// Run `go generate ./...` from the go/ directory to regenerate it.")
	fmt.Fprintln(outFile, `package msgs`)
	fmt.Fprintln(outFile)

	fmt.Fprintln(outFile, `import "xtx/eggsfs/bincode"`)
	fmt.Fprintln(outFile)

	for _, typ := range types {
		generate(outFile, typ)
	}
}
