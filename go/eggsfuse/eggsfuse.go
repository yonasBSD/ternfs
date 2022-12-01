package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"runtime/pprof"
	"sync"
	"syscall"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"

	"github.com/hanwen/go-fuse/v2/fs"
	"github.com/hanwen/go-fuse/v2/fuse"
)

var client *lib.Client
var log *lib.Logger
var dirInfoCache *lib.DirInfoCache
var readBufPool *lib.ReadSpanBufPool
var writeBufPool *sync.Pool

type statCache struct {
	size  uint64
	mtime msgs.EggsTime
}

var fileStatCacheMu sync.RWMutex
var fileStatCache map[msgs.InodeId]statCache

func eggsErrToErrno(err error) syscall.Errno {
	switch err {
	case msgs.INTERNAL_ERROR:
		return syscall.EIO
	case msgs.FATAL_ERROR:
		return syscall.EIO
	case msgs.TIMEOUT:
		return syscall.EIO
	case msgs.NOT_AUTHORISED:
		return syscall.EACCES
	case msgs.UNRECOGNIZED_REQUEST:
		return syscall.EIO
	case msgs.FILE_NOT_FOUND:
		return syscall.ENOENT
	case msgs.DIRECTORY_NOT_FOUND:
		return syscall.ENOENT
	case msgs.NAME_NOT_FOUND:
		return syscall.ENOENT
	case msgs.TYPE_IS_DIRECTORY:
		return syscall.EISDIR
	case msgs.TYPE_IS_NOT_DIRECTORY:
		return syscall.ENOTDIR
	case msgs.BAD_COOKIE:
		return syscall.EACCES
	case msgs.INCONSISTENT_STORAGE_CLASS_PARITY:
		return syscall.EINVAL
	case msgs.LAST_SPAN_STATE_NOT_CLEAN:
		return syscall.EBUSY // reasonable?
	case msgs.COULD_NOT_PICK_BLOCK_SERVICES:
		return syscall.EIO
	case msgs.BAD_SPAN_BODY:
		return syscall.EINVAL
	case msgs.SPAN_NOT_FOUND:
		return syscall.EINVAL
	case msgs.BLOCK_SERVICE_NOT_FOUND:
		return syscall.EIO
	case msgs.CANNOT_CERTIFY_BLOCKLESS_SPAN:
		return syscall.EINVAL
	case msgs.BAD_BLOCK_PROOF:
		return syscall.EINVAL
	case msgs.CANNOT_OVERRIDE_NAME:
		return syscall.EEXIST
	case msgs.NAME_IS_LOCKED:
		return syscall.EEXIST
	case msgs.MTIME_IS_TOO_RECENT:
		return syscall.EBUSY // reasonable?
	case msgs.MISMATCHING_TARGET:
		return syscall.EINVAL
	case msgs.MISMATCHING_OWNER:
		return syscall.EINVAL
	case msgs.DIRECTORY_NOT_EMPTY:
		return syscall.ENOTEMPTY
	case msgs.FILE_IS_TRANSIENT:
		return syscall.EBUSY // reasonable?
	case msgs.OLD_DIRECTORY_NOT_FOUND:
		return syscall.ENOENT
	case msgs.NEW_DIRECTORY_NOT_FOUND:
		return syscall.ENOENT
	case msgs.LOOP_IN_DIRECTORY_RENAME:
		return syscall.ELOOP
	default:
		panic(fmt.Errorf("unknown error %v", err))
	}
}

func inodeTypeToMode(typ msgs.InodeType) uint32 {
	mode := uint32(0)
	// This filesystem is read only, and permissionless.
	mode |= syscall.S_IRUSR | syscall.S_IXUSR
	mode |= syscall.S_IRGRP | syscall.S_IXGRP
	mode |= syscall.S_IROTH | syscall.S_IXOTH
	if typ == msgs.FILE {
		mode |= syscall.S_IFREG
	}
	if typ == msgs.SYMLINK {
		mode |= syscall.S_IFLNK
	}
	if typ == msgs.DIRECTORY {
		mode |= syscall.S_IFDIR
	}
	return mode
}

func shardRequest(shid msgs.ShardId, req msgs.ShardRequest, resp msgs.ShardResponse) syscall.Errno {
	if err := client.ShardRequest(log, shid, req, resp); err != nil {
		return eggsErrToErrno(err)
	}

	return 0
}

func cdcRequest(req msgs.CDCRequest, resp msgs.CDCResponse) syscall.Errno {
	if err := client.CDCRequest(log, req, resp); err != nil {
		switch eggsErr := err.(type) {
		case msgs.ErrCode:
			return eggsErrToErrno(eggsErr)
		}
		panic(err)
	}

	return 0
}

type transientFile struct {
	mu           sync.Mutex
	dir          msgs.InodeId
	valid        bool
	id           msgs.InodeId
	cookie       [8]byte
	name         string
	written      uint64 // what's already in spans
	data         *[]byte
	spanPolicy   *msgs.SpanPolicy
	blockPolicy  *msgs.BlockPolicy
	stripePolicy *msgs.StripePolicy
}

type eggsNode struct {
	fs.Inode
	id msgs.InodeId
}

func getattr(id msgs.InodeId, out *fuse.Attr) syscall.Errno {
	log.Debug("getattr inode=%v", id)

	out.Ino = uint64(id)
	out.Mode = inodeTypeToMode(id.Type())
	if id.Type() == msgs.DIRECTORY {
		resp := msgs.StatDirectoryResp{}
		if err := shardRequest(id.Shard(), &msgs.StatDirectoryReq{Id: id}, &resp); err != 0 {
			return err
		}
	} else {
		fileStatCacheMu.RLock()
		cached, found := fileStatCache[id]
		fileStatCacheMu.RUnlock()

		if !found {
			resp := msgs.StatFileResp{}
			if err := shardRequest(id.Shard(), &msgs.StatFileReq{Id: id}, &resp); err != 0 {
				return err
			}
			cached.mtime = resp.Mtime
			cached.size = resp.Size
			fileStatCacheMu.Lock()
			fileStatCache[id] = cached
			fileStatCacheMu.Unlock()
		}

		log.Debug("getattr size=%v", cached.size)
		out.Size = cached.size
		mtime := msgs.EGGS_EPOCH + uint64(cached.mtime)
		mtimesec := mtime / 1000000000
		mtimens := uint32(mtime % 1000000000)
		out.Ctime = mtimesec
		out.Ctimensec = mtimens
		out.Mtime = mtimesec
		out.Mtimensec = mtimens
	}
	return 0
}

func (n *eggsNode) Getattr(ctx context.Context, f fs.FileHandle, out *fuse.AttrOut) syscall.Errno {
	return getattr(n.id, &out.Attr)
}

func (n *eggsNode) Lookup(
	ctx context.Context, name string, out *fuse.EntryOut,
) (*fs.Inode, syscall.Errno) {
	log.Debug("lookup dir=%v, name=%v", n.id, name)
	resp := msgs.LookupResp{}
	if err := shardRequest(n.id.Shard(), &msgs.LookupReq{DirId: n.id, Name: name}, &resp); err != 0 {
		return nil, err
	}
	mode := uint32(0)
	switch resp.TargetId.Type() {
	case msgs.DIRECTORY:
		mode = syscall.S_IFDIR
	case msgs.FILE:
		mode = syscall.S_IFREG
	case msgs.SYMLINK:
		mode = syscall.S_IFLNK
	default:
		panic(fmt.Errorf("bad type %v", resp.TargetId.Type()))
	}
	if err := getattr(resp.TargetId, &out.Attr); err != 0 {
		return nil, err
	}
	return n.NewInode(ctx, &eggsNode{id: resp.TargetId}, fs.StableAttr{Ino: uint64(resp.TargetId), Mode: mode}), 0
}

type dirStream struct {
	dirId  msgs.InodeId
	cursor int
	resp   msgs.ReadDirResp
}

func (ds *dirStream) refresh() syscall.Errno {
	if err := shardRequest(ds.dirId.Shard(), &msgs.ReadDirReq{DirId: ds.dirId, StartHash: ds.resp.NextHash}, &ds.resp); err != 0 {
		return err
	}
	ds.cursor = 0
	return 0
}

func (ds *dirStream) ensureNext() (bool, syscall.Errno) {
	if ds.cursor < len(ds.resp.Results) { // we have the result right here
		return true, 0
	}
	if ds.resp.NextHash == 0 { // there's nothing more
		return false, 0
	}
	// refresh and recurse
	if err := ds.refresh(); err != 0 {
		return false, err
	}
	return ds.ensureNext()
}

func (ds *dirStream) HasNext() bool {
	hasNext, err := ds.ensureNext()
	if err != 0 {
		log.RaiseAlert(fmt.Errorf("dropping err in HasNext(): %v", err))
	}
	return hasNext
}

func (ds *dirStream) Next() (fuse.DirEntry, syscall.Errno) {
	hasNext, err := ds.ensureNext()
	var de fuse.DirEntry
	if err != 0 {
		return de, err
	}
	if !hasNext {
		panic(fmt.Errorf("expecting next, possible race?"))
	}
	edge := ds.resp.Results[ds.cursor]
	de.Ino = uint64(edge.TargetId)
	de.Mode = inodeTypeToMode(edge.TargetId.Type())
	de.Name = edge.Name
	ds.cursor++
	return de, 0
}

func (ds *dirStream) Close() {}

func (n *eggsNode) Readdir(ctx context.Context) (fs.DirStream, syscall.Errno) {
	log.Debug("readdir dir=%v", n.id)
	ds := dirStream{dirId: n.id}
	if err := ds.refresh(); err != 0 {
		return nil, err
	}
	return &ds, 0

}

func (n *eggsNode) createInternal(name string, mode uint32) (tf *transientFile, errno syscall.Errno) {
	// TODO would probably be better to check the mode/flags and return
	// EINVAL if it doesn't match what we want.
	spanPolicy := &msgs.SpanPolicy{}
	if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, n.id, spanPolicy); err != nil {
		return nil, eggsErrToErrno(err)
	}
	blockPolicy := &msgs.BlockPolicy{}
	if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, n.id, blockPolicy); err != nil {
		return nil, eggsErrToErrno(err)
	}
	stripePolicy := &msgs.StripePolicy{}
	if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, n.id, stripePolicy); err != nil {
		return nil, eggsErrToErrno(err)
	}
	req := msgs.ConstructFileReq{Note: name}
	resp := msgs.ConstructFileResp{}
	if (mode & syscall.S_IFMT) == syscall.S_IFREG {
		req.Type = msgs.FILE
	} else if (mode & syscall.S_IFMT) == syscall.S_IFLNK {
		req.Type = msgs.SYMLINK
	} else {
		panic(fmt.Errorf("bad mode %v", mode))
	}
	if err := shardRequest(n.id.Shard(), &req, &resp); err != 0 {
		return nil, err
	}
	data := writeBufPool.Get().(*[]byte)
	log.Debug("gotten data %p", data)
	transient := transientFile{
		id:           resp.Id,
		cookie:       resp.Cookie,
		valid:        true,
		name:         name,
		dir:          n.id,
		data:         data,
		spanPolicy:   spanPolicy,
		blockPolicy:  blockPolicy,
		stripePolicy: stripePolicy,
	}
	return &transient, 0
}

func (n *eggsNode) Create(
	ctx context.Context, name string, flags uint32, mode uint32, out *fuse.EntryOut,
) (node *fs.Inode, fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	log.Debug("create dir=%v, name=%v, flags=0x%08x, mode=0x%08x", n.id, name, flags, mode)

	tf, err := n.createInternal(name, mode)
	if err != 0 {
		return nil, nil, 0, err
	}
	fileNode := eggsNode{
		id: tf.id,
	}
	return n.NewInode(ctx, &fileNode, fs.StableAttr{Ino: uint64(tf.id), Mode: mode}), tf, 0, 0
}

func (n *eggsNode) Mkdir(
	ctx context.Context, name string, mode uint32, out *fuse.EntryOut,
) (*fs.Inode, syscall.Errno) {
	log.Debug("mkdir dir=%v, name=%v, mode=0x%08x", n.id, name, mode)

	// TODO would probably be better to check the mode and return
	// EINVAL if it doesn't match what we want.
	req := msgs.MakeDirectoryReq{
		OwnerId: n.id,
		Name:    name,
	}
	resp := msgs.MakeDirectoryResp{}
	if err := cdcRequest(&req, &resp); err != 0 {
		return nil, err
	}
	return n.NewInode(ctx, &eggsNode{id: resp.Id}, fs.StableAttr{Ino: uint64(resp.Id), Mode: syscall.S_IFDIR}), 0
}

func (f *transientFile) writeSpan() syscall.Errno {
	if f.data == nil {
		return 0 // happens when dup is called on file
	}
	maxSize := int(f.spanPolicy.Entries[len(f.spanPolicy.Entries)-1].MaxSize)
	boundary := maxSize
	if len(*f.data) < maxSize {
		boundary = len(*f.data)
	}
	spanData := (*f.data)[:boundary]
	leftover := append([]byte{}, (*f.data)[boundary:]...)
	spanSize := uint32(len(spanData))
	if spanSize == 0 {
		return 0
	}
	var err error
	*f.data, err = client.CreateSpan(log, []msgs.BlockServiceId{}, f.spanPolicy, f.blockPolicy, f.stripePolicy, f.id, f.cookie, f.written, spanSize, spanData)
	if err != nil {
		f.valid = false
		return eggsErrToErrno(err)
	}
	f.written += uint64(spanSize)
	*f.data = append((*f.data)[:0], leftover...)

	return 0
}

func (f *transientFile) writeSpanIfNecessary() syscall.Errno {
	biggestSpan := f.spanPolicy.Entries[len(f.spanPolicy.Entries)-1]
	if len(*f.data) < int(biggestSpan.MaxSize) {
		return 0
	}
	return f.writeSpan()
}

func (f *transientFile) Write(ctx context.Context, data []byte, off int64) (written uint32, errno syscall.Errno) {
	log.Debug("write file=%v, off=%v, count=%v", f.id, off, len(data))

	f.mu.Lock()
	defer f.mu.Unlock()

	if !f.valid {
		return 0, syscall.EBADF
	}

	// TODO support writing in the middle (and flushing out the span later)
	if off != int64(f.written)+int64(len(*f.data)) {
		log.Info("refusing to write in the past")
		return 0, syscall.EINVAL
	}

	*f.data = append(*f.data, data...)

	if err := f.writeSpanIfNecessary(); err != 0 {
		log.Debug("writing span failed with error %v, invalidating %v", err, f.id)
		f.valid = false
		return 0, err
	}

	return uint32(len(data)), 0
}

func (f *transientFile) Flush(ctx context.Context) syscall.Errno {
	log.Debug("flush file=%v", f.id)

	f.mu.Lock()
	defer f.mu.Unlock()

	if !f.valid {
		log.Debug("tf %v is not valid, returning EBADF", f.id)
		return syscall.EBADF
	}

	defer func() {
		writeBufPool.Put(f.data)
		f.data = nil
		f.valid = false
	}()

	if err := f.writeSpan(); err != 0 {
		return err
	}

	req := msgs.LinkFileReq{
		FileId:  f.id,
		Cookie:  f.cookie,
		OwnerId: f.dir,
		Name:    f.name,
	}
	if err := shardRequest(f.dir.Shard(), &req, &msgs.LinkFileResp{}); err != 0 {
		return err
	}

	return 0
}

func (n *eggsNode) Setattr(ctx context.Context, f fs.FileHandle, in *fuse.SetAttrIn, out *fuse.AttrOut) syscall.Errno {
	log.Debug("setattr inode=%v, in=%+v", n.id, in)

	return 0
}

func (n *eggsNode) Rename(ctx context.Context, oldName string, newParent0 fs.InodeEmbedder, newName string, flags uint32) syscall.Errno {
	oldParent := n.id
	newParent := newParent0.(*eggsNode).id

	log.Debug("rename dir=%v oldParent=%v, oldName=%v, newParent=%v, newName=%v", n, oldParent, oldName, newParent, newName)

	var targetId msgs.InodeId
	var oldCreationTime msgs.EggsTime
	{
		req := msgs.LookupReq{DirId: oldParent, Name: oldName}
		resp := msgs.LookupResp{}
		if err := shardRequest(oldParent.Shard(), &req, &resp); err != 0 {
			return err
		}
		targetId = resp.TargetId
		oldCreationTime = resp.CreationTime
	}

	if oldParent == newParent {
		req := msgs.SameDirectoryRenameReq{
			TargetId:        targetId,
			DirId:           oldParent,
			OldName:         oldName,
			OldCreationTime: oldCreationTime,
			NewName:         newName,
		}
		if err := shardRequest(oldParent.Shard(), &req, &msgs.SameDirectoryRenameResp{}); err != 0 {
			return err
		}
	} else if targetId.Type() == msgs.DIRECTORY {
		req := msgs.RenameDirectoryReq{
			TargetId:        targetId,
			OldOwnerId:      oldParent,
			OldName:         oldName,
			OldCreationTime: oldCreationTime,
			NewOwnerId:      newParent,
			NewName:         newName,
		}
		if err := cdcRequest(&req, &msgs.RenameDirectoryResp{}); err != 0 {
			return err
		}
	} else {
		req := msgs.RenameFileReq{
			TargetId:        targetId,
			OldOwnerId:      oldParent,
			OldName:         oldName,
			OldCreationTime: oldCreationTime,
			NewOwnerId:      newParent,
			NewName:         newName,
		}
		if err := cdcRequest(&req, &msgs.RenameFileResp{}); err != 0 {
			return err
		}
	}

	return 0
}

// We keep information for the last requested span,
type openFile struct {
	mu   sync.Mutex
	id   msgs.InodeId
	size uint64 // total size of file

	spanReader lib.PuttableReadCloser // the span we're currently reading from
	readOffset int64                  // the offset we're currently reading at, in the span reader above

	// We store the last successful span resp, to avoid re-issuing it if we can
	spans *msgs.FileSpansResp
}

func (n *eggsNode) Open(ctx context.Context, flags uint32) (fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	log.Debug("open file=%v", n.id)

	statResp := msgs.StatFileResp{}
	if err := shardRequest(n.id.Shard(), &msgs.StatFileReq{Id: n.id}, &statResp); err != 0 {
		return nil, 0, err
	}
	of := openFile{
		id:         n.id,
		size:       statResp.Size,
		readOffset: -1,
	}
	return &of, 0, 0
}

func (of *openFile) lookupSpan(offset int64) ([]msgs.BlockService, *msgs.FetchedSpan) {
	for i := 0; i < len(of.spans.Spans); i++ {
		span := &of.spans.Spans[i]
		if int64(span.Header.ByteOffset) <= offset && offset < int64(span.Header.ByteOffset)+int64(span.Header.Size) {
			log.Debug("picking span starting at %v, of size %v", span.Header.ByteOffset, span.Header.Size)
			return of.spans.BlockServices, span
		}
	}
	return nil, nil
}

func (of *openFile) getSpan(offset int64) ([]msgs.BlockService, *msgs.FetchedSpan, syscall.Errno) {
	// check if we're within current span index
	if of.spans != nil {
		blockServices, span := of.lookupSpan(offset)
		if blockServices != nil {
			return blockServices, span, 0
		}
	}
	// we need to fetch the spans again
	of.spans = &msgs.FileSpansResp{}
	if err := shardRequest(of.id.Shard(), &msgs.FileSpansReq{FileId: of.id, ByteOffset: uint64(offset)}, of.spans); err != 0 {
		return nil, nil, eggsErrToErrno(err)
	}
	blockServices, span := of.lookupSpan(offset)
	if blockServices == nil {
		panic(fmt.Errorf("couldn't get span at offset %v", offset))
	}
	return blockServices, span, 0
}

// ensures that the span reader is at the current offset.
func (of *openFile) getSpanReaderAt(offset int64) syscall.Errno {
	if offset < 0 || offset >= int64(of.size) {
		panic(fmt.Errorf("offset=%v < 0 || offset=%v >= of.size=%v", offset, offset, of.size))
	}
	if of.spanReader != nil {
		panic(fmt.Errorf("we already have a span reader, close it explicitly first (it's non obvious how to from here)"))
	}
	blockServices, fetchedSpan, err := of.getSpan(offset)
	if err != 0 {
		return err
	}
	{
		var err error
		of.spanReader, err = client.ReadSpan(log, readBufPool, []msgs.BlockServiceId{}, blockServices, fetchedSpan)
		if err != nil {
			return eggsErrToErrno(err)
		}
	}
	of.readOffset = int64(fetchedSpan.Header.ByteOffset)
	// fast forward to what we're interested
	var buf [256]byte
	for of.readOffset < offset {
		end := 256
		if offset-of.readOffset < 256 {
			end = int(offset - of.readOffset)
		}
		read, err := of.spanReader.Read(buf[:end])
		if err != nil {
			return eggsErrToErrno(err)
		}
		of.readOffset += int64(read)
	}
	return 0
}

// One step of reading, will go through at most one span.
func (of *openFile) readInternal(dest []byte, off int64) (int64, syscall.Errno) {
	if len(dest) == 0 {
		return 0, 0
	}

	// Check if we're still within the file: if not, we can just exit
	if off >= int64(of.size) {
		log.Debug("%v is beyond %v, nothing to read", off, of.size)
		return 0, 0
	}

	// If the offset has changed, reset
	if off != of.readOffset {
		log.Debug("mismatching offset (%v vs %v), will reset span", off, of.readOffset)
		if of.spanReader != nil {
			of.spanReader.Close()
			of.spanReader = nil
		}
		if err := of.getSpanReaderAt(off); err != 0 {
			return 0, err
		}
	}

	read, err := of.spanReader.Read(dest)
	if err == io.EOF { // load next span
		log.Debug("finished reading current span, loading next")
		// close normally here so that we can reuse the connections
		of.spanReader.Close()
		of.spanReader = nil
		if err := of.getSpanReaderAt(off); err != 0 {
			return 0, err
		}
		read, err = of.spanReader.Read(dest)
	}

	of.readOffset += int64(read)

	if err != nil {
		return int64(read), eggsErrToErrno(err)
	}
	return int64(read), 0
}

func (of *openFile) Flush(ctx context.Context) syscall.Errno {
	if of.spanReader != nil {
		of.spanReader.Close()
	}
	return 0
}

func (of *openFile) Read(ctx context.Context, dest []byte, off int64) (fuse.ReadResult, syscall.Errno) {
	log.Debug("read file=%v, off=%v, count=%v", of.id, off, len(dest))

	of.mu.Lock()
	defer of.mu.Unlock()

	internalOff := int64(0)
	// TODO for some reason go-fuse does not seem to support partial reads in the middle of the
	// file, which is weird. So we fully drain it. But we should understand what's going on.
	for {
		read, err := of.readInternal(dest[internalOff:], off+internalOff)
		if err != 0 {
			return nil, err
		}
		internalOff += read
		if internalOff == int64(len(dest)) || read == 0 {
			break
		}
	}
	log.Debug("read %v bytes", internalOff)
	return fuse.ReadResultData(dest[:internalOff]), 0
}

func (n *eggsNode) Unlink(ctx context.Context, name string) syscall.Errno {
	log.Debug("unlink dir=%v, name=%v", n.id, name)

	lookupResp := msgs.LookupResp{}
	if err := shardRequest(n.id.Shard(), &msgs.LookupReq{DirId: n.id, Name: name}, &lookupResp); err != 0 {
		return err
	}
	unlinkReq := msgs.SoftUnlinkFileReq{
		OwnerId:      n.id,
		FileId:       lookupResp.TargetId,
		Name:         name,
		CreationTime: lookupResp.CreationTime,
	}
	return shardRequest(n.id.Shard(), &unlinkReq, &msgs.SoftUnlinkFileResp{})
}

func (n *eggsNode) Rmdir(ctx context.Context, name string) syscall.Errno {
	log.Debug("rmdir dir=%v, name=%v", n.id, name)
	lookupResp := msgs.LookupResp{}
	if err := shardRequest(n.id.Shard(), &msgs.LookupReq{DirId: n.id, Name: name}, &lookupResp); err != 0 {
		return err
	}
	unlinkReq := msgs.SoftUnlinkDirectoryReq{
		OwnerId:      n.id,
		TargetId:     lookupResp.TargetId,
		Name:         name,
		CreationTime: lookupResp.CreationTime,
	}
	return cdcRequest(&unlinkReq, &msgs.SoftUnlinkDirectoryResp{})
}

func (n *eggsNode) Symlink(ctx context.Context, target, name string, out *fuse.EntryOut) (node *fs.Inode, errno syscall.Errno) {
	log.Debug("symlink dir=%v, target=%v, name=%v", n.id, target, name)
	tf, err := n.createInternal(name, syscall.S_IFLNK)
	if err != 0 {
		return nil, err
	}
	if _, err := tf.Write(ctx, []byte(target), 0); err != 0 {
		return nil, err
	}
	if err := tf.Flush(ctx); err != 0 {
		return nil, err
	}
	return n.NewInode(ctx, &eggsNode{id: tf.id}, fs.StableAttr{Ino: uint64(tf.id), Mode: syscall.S_IFLNK}), 0
}

func (n *eggsNode) Readlink(ctx context.Context) ([]byte, syscall.Errno) {
	log.Debug("readlink file=%v", n.id)

	if n.id.Type() != msgs.SYMLINK {
		return nil, syscall.EINVAL
	}

	resp := msgs.FileSpansResp{}
	if err := shardRequest(n.id.Shard(), &msgs.FileSpansReq{FileId: n.id}, &resp); err != 0 {
		return nil, err
	}
	if len(resp.Spans) > 1 {
		panic(fmt.Errorf("more than one span for symlink"))
	}
	spanReader, err := client.ReadSpan(log, readBufPool, []msgs.BlockServiceId{}, resp.BlockServices, &resp.Spans[0])
	if err != nil {
		return nil, eggsErrToErrno(err)

	}
	bs, err := io.ReadAll(spanReader)
	if err != nil {
		return nil, eggsErrToErrno(err)
	}
	return bs, 0
}

var _ = (fs.InodeEmbedder)((*eggsNode)(nil))
var _ = (fs.NodeLookuper)((*eggsNode)(nil))
var _ = (fs.NodeReaddirer)((*eggsNode)(nil))
var _ = (fs.NodeMkdirer)((*eggsNode)(nil))
var _ = (fs.NodeGetattrer)((*eggsNode)(nil))
var _ = (fs.NodeCreater)((*eggsNode)(nil))
var _ = (fs.NodeSetattrer)((*eggsNode)(nil))
var _ = (fs.NodeRenamer)((*eggsNode)(nil))
var _ = (fs.NodeOpener)((*eggsNode)(nil))
var _ = (fs.NodeUnlinker)((*eggsNode)(nil))
var _ = (fs.NodeRmdirer)((*eggsNode)(nil))
var _ = (fs.NodeSymlinker)((*eggsNode)(nil))
var _ = (fs.NodeReadlinker)((*eggsNode)(nil))

var _ = (fs.FileWriter)((*transientFile)(nil))
var _ = (fs.FileFlusher)((*transientFile)(nil))

var _ = (fs.FileReader)((*openFile)(nil))
var _ = (fs.FileFlusher)((*openFile)(nil))

func terminate(server *fuse.Server, terminated *bool) {
	log.Info("terminating")
	if *terminated {
		log.Info("already terminated")
		return
	}
	log.Info("stopping cpu profile")
	pprof.StopCPUProfile()
	log.Info("about to terminate")
	*terminated = true
	if err := server.Unmount(); err != nil {
		log.Info("could not unmount: %v\n", err)
	}
}

func usage() {
	fmt.Fprintf(os.Stderr, "Usage: %s [options] <mountpoint>\n", os.Args[0])
	flag.PrintDefaults()
}

func main() {
	verbose := flag.Bool("verbose", false, "Enables debug logging.")
	trace := flag.Bool("trace", false, "")
	logFile := flag.String("log-file", "", "Redirect logging output to given file.")
	signalParent := flag.Bool("signal-parent", false, "If passed, will send USR1 to parent when ready -- useful for tests.")
	shuckleAddress := flag.String("shuckle", lib.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	profileFile := flag.String("profile-file", "", "If set, will write CPU profile here.")
	flag.Usage = usage
	flag.Parse()

	if flag.NArg() != 1 {
		usage()
		os.Exit(2)
	}
	mountPoint := flag.Args()[0]

	if mountPoint == "" {
		fmt.Fprintf(os.Stderr, "Please specify mountpoint with -mountpoint\n")
		os.Exit(2)
	}

	logOut := os.Stdout
	if *logFile != "" {
		var err error
		logOut, err = os.OpenFile(*logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			fmt.Fprintf(os.Stderr, "could not open file %v: %v", logFile, err)
			os.Exit(1)
		}
		defer logOut.Close()
	}
	logger := lib.NewLoggerLogger(logOut)
	level := lib.INFO
	if *verbose {
		level = lib.DEBUG
	}
	if *trace {
		level = lib.TRACE
	}
	log = lib.NewLoggerFromLogger(level, logger)

	if *profileFile != "" {
		f, err := os.Create(*profileFile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Could not open profile file %v", *profileFile)
			os.Exit(1)
		}
		pprof.StartCPUProfile(f) // we stop in terminate()
	}

	counters := &lib.ClientCounters{}

	var err error
	client, err = lib.NewClient(log, *shuckleAddress, nil, counters, nil)
	if err != nil {
		panic(err)
	}

	dirInfoCache = lib.NewDirInfoCache()

	fileStatCache = make(map[msgs.InodeId]statCache)

	writeBufPool = &sync.Pool{
		New: func() any {
			buf := []byte{}
			return &buf
		},
	}

	readBufPool = lib.NewReadSpanBufPool()

	root := eggsNode{
		id: msgs.ROOT_DIR_INODE_ID,
	}
	fuseOptions := &fs.Options{
		Logger: logger,
	}
	// fuseOptions.Debug = *trace
	server, err := fs.Mount(mountPoint, &root, fuseOptions)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not mount: %v", err)
		os.Exit(1)
	}

	log.Info("mounted at %v", mountPoint)

	if *signalParent {
		log.Info("sending USR1 to parent")
		if err := syscall.Kill(os.Getppid(), syscall.SIGUSR1); err != nil {
			panic(err)
		}
	}

	// print out stats when sent USR1
	{
		statsChan := make(chan os.Signal, 1)
		signal.Notify(statsChan, syscall.SIGUSR1)
		go func() {
			for {
				<-statsChan
				counters.Log(log)
			}
		}()
	}

	terminated := false
	defer terminate(server, &terminated)
	// Cleanup if we get killed with a signal. Obviously we can't do much
	// in the case of SIGKILL or SIGQUIT.
	signalChan := make(chan os.Signal, 1)
	signal.Notify(signalChan, syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGILL, syscall.SIGTRAP, syscall.SIGABRT, syscall.SIGSTKFLT, syscall.SIGSYS)
	go func() {
		sig := <-signalChan
		signal.Stop(signalChan)
		terminate(server, &terminated)
		syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
	}()

	server.Wait()
}
