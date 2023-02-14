package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"runtime/pprof"
	"sync"
	"syscall"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"

	"github.com/hanwen/go-fuse/v2/fs"
	"github.com/hanwen/go-fuse/v2/fuse"
)

var client *eggs.Client
var log *eggs.Logger
var dirInfoCache *eggs.DirInfoCache

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
	mu      sync.Mutex
	dir     msgs.InodeId
	valid   bool
	id      msgs.InodeId
	cookie  [8]byte
	name    string
	written uint64
	buf     *bytes.Buffer
	dirInfo *msgs.DirectoryInfoBody
}

type eggsNode struct {
	fs.Inode
	id msgs.InodeId
}

func (n *eggsNode) Lookup(
	ctx context.Context, name string, out *fuse.EntryOut,
) (*fs.Inode, syscall.Errno) {
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
	ds := dirStream{dirId: n.id}
	if err := ds.refresh(); err != 0 {
		return nil, err
	}
	return &ds, 0

}

func (n *eggsNode) createInternal(name string, mode uint32) (tf *transientFile, errno syscall.Errno) {
	// TODO would probably be better to check the mode/flags and return
	// EINVAL if it doesn't match what we want.
	dirInfo, err := eggs.ResolveDirectoryInfo(log, client, dirInfoCache, n.id)
	if err != nil {
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
	transient := transientFile{
		id:      resp.Id,
		cookie:  resp.Cookie,
		valid:   true,
		name:    name,
		dir:     n.id,
		buf:     bytes.NewBuffer([]byte{}),
		dirInfo: dirInfo,
	}
	return &transient, 0
}

func (n *eggsNode) Create(
	ctx context.Context, name string, flags uint32, mode uint32, out *fuse.EntryOut,
) (node *fs.Inode, fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
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
	// TODO would probably be better to check the mode and return
	// EINVAL if it doesn't match what we want.
	req := msgs.MakeDirectoryReq{
		OwnerId: n.id,
		Name:    name,
		Info:    msgs.SetDirectoryInfo{Inherited: true},
	}
	resp := msgs.MakeDirectoryResp{}
	if err := cdcRequest(&req, &resp); err != 0 {
		return nil, err
	}
	return n.NewInode(ctx, &eggsNode{id: resp.Id}, fs.StableAttr{Ino: uint64(resp.Id), Mode: syscall.S_IFDIR}), 0
}

func (n *eggsNode) Getattr(ctx context.Context, f fs.FileHandle, out *fuse.AttrOut) syscall.Errno {
	out.Ino = uint64(n.id)
	out.Mode = inodeTypeToMode(n.id.Type())
	if n.id.Type() == msgs.DIRECTORY {
		resp := msgs.StatDirectoryResp{}
		if err := shardRequest(n.id.Shard(), &msgs.StatDirectoryReq{Id: n.id}, &resp); err != 0 {
			return err
		}
	} else {
		resp := msgs.StatFileResp{}
		if err := shardRequest(n.id.Shard(), &msgs.StatFileReq{Id: n.id}, &resp); err != 0 {
			return err
		}
		out.Size = resp.Size
		mtime := msgs.EGGS_EPOCH + uint64(resp.Mtime)
		mtimesec := mtime / 1000000000
		mtimens := uint32(mtime % 1000000000)
		out.Ctime = mtimesec
		out.Ctimensec = mtimens
		out.Mtime = mtimesec
		out.Mtimensec = mtimens
	}
	return 0
}

// If policy=nil, we want an inline span
func (f *transientFile) writeSpan(policy *msgs.SpanPolicy) syscall.Errno {
	if policy != nil && policy.Parity.DataBlocks() != 1 {
		panic(fmt.Errorf("only mirroring supported"))
	}

	maxSize := 255
	if policy != nil {
		maxSize = int(policy.MaxSize)
	}
	data := f.buf.Next(maxSize)
	crc := eggs.CRC32C(data)

	initiateReq := msgs.AddSpanInitiateReq{
		FileId:     f.id,
		Cookie:     f.cookie,
		ByteOffset: f.written,
		Crc32:      crc,
		Size:       uint64(len(data)),
	}
	if len(data) < 256 {
		if policy != nil {
			panic(fmt.Errorf("unexpected non-nil policy"))
		}
		initiateReq.StorageClass = msgs.INLINE_STORAGE
		initiateReq.BodyBytes = data
	} else {
		initiateReq.BlockSize = uint64(len(data))
		initiateReq.StorageClass = policy.StorageClass
		initiateReq.Parity = policy.Parity
		initiateReq.BodyBlocks = make([]msgs.NewBlockInfo, policy.Parity.Blocks())
		for i := 0; i < policy.Parity.Blocks(); i++ {
			initiateReq.BodyBlocks[i].Crc32 = crc
		}
	}
	initiateResp := msgs.AddSpanInitiateResp{}

	if err := shardRequest(f.id.Shard(), &initiateReq, &initiateResp); err != 0 {
		return err
	}

	if len(data) > 255 {
		certifyReq := msgs.AddSpanCertifyReq{
			FileId:     f.id,
			Cookie:     f.cookie,
			ByteOffset: f.written,
			Proofs:     make([]msgs.BlockProof, len(initiateResp.Blocks)),
		}
		for i, block := range initiateResp.Blocks {
			conn, err := eggs.BlockServiceConnection(log, block.BlockServiceId, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2)
			if err != nil {
				return syscall.EIO
			}
			proof, err := eggs.WriteBlock(log, conn, &block, bytes.NewReader(data), uint32(len(data)), crc)
			conn.Close()
			if err != nil {
				return syscall.EIO
			}
			certifyReq.Proofs[i].BlockId = block.BlockId
			certifyReq.Proofs[i].Proof = proof
		}
		if err := shardRequest(f.id.Shard(), &certifyReq, &msgs.AddSpanCertifyResp{}); err != 0 {
			return err
		}
	}

	f.written += uint64(len(data))

	return 0
}

func (f *transientFile) writeSpanIfNecessary() syscall.Errno {
	biggestSpan := f.dirInfo.SpanPolicies[len(f.dirInfo.SpanPolicies)-1]
	if len(f.buf.Bytes()) < int(biggestSpan.MaxSize) {
		return 0
	}

	return f.writeSpan(&biggestSpan)
}

func (f *transientFile) Write(ctx context.Context, data []byte, off int64) (written uint32, errno syscall.Errno) {
	f.mu.Lock()
	defer f.mu.Unlock()

	if !f.valid {
		return 0, syscall.EBADF
	}

	log.Debug("writing %v at %v, %v bytes", f.id, off, len(data))

	// TODO support writing in the middle (and flushing out the span later)
	if off != int64(f.written)+int64(len(f.buf.Bytes())) {
		log.Info("refusing to write in the past")
		return 0, syscall.EINVAL
	}

	f.buf.Write(data)

	if err := f.writeSpanIfNecessary(); err != 0 {
		f.valid = false
		return 0, err
	}

	return uint32(len(data)), 0
}

func (f *transientFile) Flush(ctx context.Context) syscall.Errno {
	f.mu.Lock()
	defer f.mu.Unlock()

	if !f.valid {
		return syscall.EBADF
	}

	for len(f.buf.Bytes()) > 255 {
		var policy *msgs.SpanPolicy
		for _, currPolicy := range f.dirInfo.SpanPolicies {
			policy = &currPolicy
			if int(currPolicy.MaxSize) > len(f.buf.Bytes()) {
				break
			}
		}
		if err := f.writeSpan(policy); err != 0 {
			f.valid = false
			return err
		}
	}
	if len(f.buf.Bytes()) > 0 {
		if err := f.writeSpan(nil); err != 0 {
			f.valid = false
			return err
		}
	}

	req := msgs.LinkFileReq{
		FileId:  f.id,
		Cookie:  f.cookie,
		OwnerId: f.dir,
		Name:    f.name,
	}
	if err := shardRequest(f.dir.Shard(), &req, &msgs.LinkFileResp{}); err != 0 {
		f.valid = false
		return err
	}

	return 0
}

func (n *eggsNode) Setattr(ctx context.Context, f fs.FileHandle, in *fuse.SetAttrIn, out *fuse.AttrOut) syscall.Errno {
	return 0
}

func (n *eggsNode) Rename(ctx context.Context, oldName string, newParent0 fs.InodeEmbedder, newName string, flags uint32) syscall.Errno {
	oldParent := n.id
	newParent := newParent0.(*eggsNode).id

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

	// We keep the latest span/block service around, we might just need
	// the next bit in sequential reads.
	spans                   msgs.FileSpansResp
	currentSpanIx           int // index in list above
	currentBlockService     *msgs.BlockService
	currentBlockServiceConn *net.TCPConn
}

func (n *eggsNode) Open(ctx context.Context, flags uint32) (fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	statResp := msgs.StatFileResp{}
	if err := shardRequest(n.id.Shard(), &msgs.StatFileReq{Id: n.id}, &statResp); err != 0 {
		return nil, 0, err
	}
	of := openFile{
		id:   n.id,
		size: statResp.Size,
	}
	if err := shardRequest(n.id.Shard(), &msgs.FileSpansReq{FileId: n.id}, &of.spans); err != 0 {
		return nil, 0, err
	}
	return &of, 0, 0
}

func (of *openFile) resetBlockService() syscall.Errno {
	if of.currentBlockService != nil {
		if err := of.currentBlockServiceConn.Close(); err != nil {
			panic(err)
		}
	}
	of.currentBlockService = nil
	of.currentBlockServiceConn = nil
	return 0
}

func (of *openFile) ensureBlockService(bs *msgs.BlockService) syscall.Errno {
	if of.currentBlockService == nil || of.currentBlockService.Id != bs.Id {
		if of.currentBlockService != nil {
			if err := of.currentBlockServiceConn.Close(); err != nil {
				panic(err)
			}
		}
		conn, err := eggs.BlockServiceConnection(log, bs.Id, bs.Ip1, bs.Port1, bs.Ip2, bs.Port2)
		if err != nil {
			panic(err)
		}
		of.currentBlockServiceConn = conn
		of.currentBlockService = bs
	}
	return 0
}

// One step of reading, will go through at most one span.
func (of *openFile) readInternal(dest []byte, off int64) (int64, syscall.Errno) {
	// Check if we're still within the file: if not, we can just exit
	if off >= int64(of.size) {
		log.Debug("%v is beyond %v, nothing to read", off, of.size)
		return 0, 0
	}

	// Check if we're still within the current span
	currentSpan := of.spans.Spans[of.currentSpanIx]
	if off < int64(currentSpan.ByteOffset) || off >= int64(currentSpan.ByteOffset+currentSpan.Size) {
		log.Debug("offset %v is not within the current span bounds [%v, %v)", off, currentSpan.ByteOffset, currentSpan.ByteOffset+currentSpan.Size)
		firstSpan := of.spans.Spans[0]
		lastSpan := of.spans.Spans[len(of.spans.Spans)-1]
		if off < int64(firstSpan.ByteOffset) || off >= int64(lastSpan.ByteOffset+lastSpan.Size) {
			log.Debug("offset %v is not within the current spanS bounds [%v, %v), will refresh", off, firstSpan.ByteOffset, lastSpan.ByteOffset+lastSpan.Size)
			// we need to refresh the spans
			if err := shardRequest(of.id.Shard(), &msgs.FileSpansReq{FileId: of.id, ByteOffset: uint64(off)}, &of.spans); err != 0 {
				return 0, err
			}
			of.currentSpanIx = 0
			// restart
			return of.readInternal(dest, off)
		} else {
			log.Debug("offset %v is within the current spanS bounds [%v, %v), will look for span starting from ix %v", off, firstSpan.ByteOffset, lastSpan.ByteOffset+lastSpan.Size, of.currentSpanIx)
			// we need to look for the correct span
			change := 1
			if off < int64(currentSpan.ByteOffset) {
				change = -1
			}
			for {
				currentSpan = of.spans.Spans[of.currentSpanIx]
				if off >= int64(currentSpan.ByteOffset) && off < int64(currentSpan.ByteOffset+currentSpan.Size) {
					break
				}
				of.currentSpanIx += change
			}
			// restart
			return of.readInternal(dest, off)
		}
	}

	spanOffset := off - int64(currentSpan.ByteOffset)
	remainingInSpan := int64(currentSpan.Size) - spanOffset
	if spanOffset < 0 || spanOffset > int64(currentSpan.Size) || remainingInSpan <= 0 {
		panic(fmt.Errorf("unexpected out of bounds spanOffset/remainingInSpan"))
	}

	if currentSpan.StorageClass == msgs.INLINE_STORAGE {
		if err := of.resetBlockService(); err != 0 {
			return 0, err
		}
		return int64(copy(dest, currentSpan.BodyBytes[spanOffset:])), 0
	} else {
		if currentSpan.Parity.DataBlocks() != 1 {
			panic(fmt.Errorf("unsupported parity %v", currentSpan.Parity))
		}
		block := &currentSpan.BodyBlocks[0]
		if err := of.ensureBlockService(&of.spans.BlockServices[block.BlockServiceIx]); err != 0 {
			return 0, err
		}
		toRead := uint32(remainingInSpan)
		if len(dest) < int(toRead) {
			toRead = uint32(len(dest))
		}
		if err := eggs.FetchBlock(log, of.currentBlockServiceConn, of.currentBlockService, block.BlockId, block.Crc32, uint32(spanOffset), toRead); err != nil {
			panic(err)
		}
		dest = dest[:toRead]
		if _, err := io.ReadFull(of.currentBlockServiceConn, dest); err != nil {
			panic(err)
		}
		return int64(len(dest)), 0
	}
}

func (of *openFile) Read(ctx context.Context, dest []byte, off int64) (fuse.ReadResult, syscall.Errno) {
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
		if read == 0 {
			break
		}
	}
	return fuse.ReadResultData(dest[:internalOff]), 0
}

func (of *openFile) Flush(ctx context.Context) syscall.Errno {
	of.mu.Lock()
	defer of.mu.Unlock()
	return of.resetBlockService()
}

func (n *eggsNode) Unlink(ctx context.Context, name string) syscall.Errno {
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
	var data []byte
	resp := msgs.FileSpansResp{}
	if err := shardRequest(n.id.Shard(), &msgs.FileSpansReq{FileId: n.id}, &resp); err != 0 {
		return nil, err
	}
	if len(resp.Spans) > 1 {
		panic(fmt.Errorf("more than one span for symlink"))
	}
	span := resp.Spans[0]
	if len(span.BodyBytes) > 0 {
		data = span.BodyBytes
	} else {
		if span.Parity.DataBlocks() > 1 {
			panic(fmt.Errorf("multiple data blocks not supported yet"))
		}
		block := span.BodyBlocks[0]
		blockService := resp.BlockServices[block.BlockServiceIx]
		conn, err := eggs.BlockServiceConnection(log, blockService.Id, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2)
		if err != nil {
			panic(err)
		}
		defer conn.Close()
		if err := eggs.FetchBlock(log, conn, &blockService, block.BlockId, block.Crc32, 0, uint32(span.BlockSize)); err != nil {
			panic(err)
		}
		data = make([]byte, span.BlockSize)
		if _, err := io.ReadFull(conn, data); err != nil {
			panic(err)
		}
	}
	return data, 0
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
	log.Info("stopping cpu pofile")
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
	logFile := flag.String("log-file", "", "Redirect logging output to given file.")
	signalParent := flag.Bool("signal-parent", false, "If passed, will send USR1 to parent when ready -- useful for tests.")
	shuckleAddress := flag.String("shuckle", eggs.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
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
	log = eggs.NewLogger(*verbose, logOut)

	if *profileFile != "" {
		f, err := os.Create(*profileFile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Could not open profile file %v", *profileFile)
			os.Exit(1)
		}
		pprof.StartCPUProfile(f) // we stop in terminate()
	}

	var err error
	client, err = eggs.NewClient(log, *shuckleAddress, nil, nil, nil)
	if err != nil {
		panic(err)
	}

	dirInfoCache = eggs.NewDirInfoCache()

	root := eggsNode{
		id: msgs.ROOT_DIR_INODE_ID,
	}
	server, err := fs.Mount(mountPoint, &root, &fs.Options{})
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
