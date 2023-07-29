package lib

import (
	"bytes"
	"fmt"
	"io"
	"path/filepath"
	"sort"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/rs"
)

type blockReader struct {
	cells    int
	cellSize int
	stride   int
	data     []byte
	cursor   int
}

func (r *blockReader) Read(p []byte) (int, error) {
	if r.cursor >= r.cells*r.cellSize {
		return 0, io.EOF
	}
	cell := r.cursor / r.cellSize
	cellCursor := r.cursor % r.cellSize
	read := copy(p, r.data[cell*r.stride+cellCursor:cell*r.stride+r.cellSize])
	r.cursor += read
	return read, nil
}

func (c *Client) createInlineSpan(
	log *Logger,
	id msgs.InodeId,
	cookie [8]byte,
	offset uint64,
	sizeWithZeros uint32,
	data []byte,
) error {
	if int(sizeWithZeros) < len(data) {
		panic(fmt.Errorf("sizeWithZeros=%v < len(data)=%v", sizeWithZeros, len(data)))
	}
	crc := crc32c.Sum(0, data)
	crc = crc32c.ZeroExtend(crc, int(sizeWithZeros)-len(data))
	req := msgs.AddInlineSpanReq{
		FileId:       id,
		Cookie:       cookie,
		StorageClass: msgs.INLINE_STORAGE,
		ByteOffset:   offset,
		Size:         sizeWithZeros,
		Body:         data,
		Crc:          msgs.Crc(crc),
	}
	if err := c.ShardRequest(log, id.Shard(), &req, &msgs.AddInlineSpanResp{}); err != nil {
		return err
	}
	return nil
}

func ensureLen(buf *[]byte, l int) {
	lenBefore := len(*buf)
	if l <= cap(*buf) {
		*buf = (*buf)[:l]
	} else {
		*buf = (*buf)[:cap(*buf)]
		*buf = append(*buf, make([]byte, l-len(*buf))...)
	}
	// memset? what's that?
	for i := lenBefore; i < len(*buf); i++ {
		(*buf)[i] = 0
	}
}

const EGGSFS_PAGE_SIZE int = 4096

func prepareSpanInitiateReq(
	blacklist []msgs.BlacklistEntry,
	spanPolicies *msgs.SpanPolicy,
	blockPolicies *msgs.BlockPolicy,
	stripePolicy *msgs.StripePolicy,
	id msgs.InodeId,
	cookie [8]byte,
	offset uint64,
	sizeWithZeros uint32,
	data *[]byte,
) *msgs.AddSpanInitiateReq {
	if int(sizeWithZeros) < len(*data) {
		panic(fmt.Errorf("sizeWithZeros=%v < len(data)=%v", sizeWithZeros, len(*data)))
	}

	crc := crc32c.Sum(0, *data)
	crc = crc32c.ZeroExtend(crc, int(sizeWithZeros)-len(*data))

	// Compute all the size parameters. We use TargetStripeSize as an upper bound,
	// for now (i.e. the stripe will always be smaller than TargetStripeSize)
	S := (len(*data) + int(stripePolicy.TargetStripeSize) - 1) / int(stripePolicy.TargetStripeSize)
	if S > 15 {
		S = 15
	}
	spanPolicy := spanPolicies.Pick(uint32(len(*data)))
	D := spanPolicy.Parity.DataBlocks()
	P := spanPolicy.Parity.ParityBlocks()
	B := spanPolicy.Parity.Blocks()
	blockSize := (len(*data) + D - 1) / D
	cellSize := (blockSize + S - 1) / S
	// Round up cell to page size
	cellSize = EGGSFS_PAGE_SIZE * ((cellSize + EGGSFS_PAGE_SIZE - 1) / EGGSFS_PAGE_SIZE)
	blockSize = cellSize * S
	storageClass := blockPolicies.Pick(uint32(blockSize)).StorageClass

	// Pad the data with zeros
	ensureLen(data, S*D*cellSize)

	initiateReq := msgs.AddSpanInitiateReq{
		FileId:       id,
		Cookie:       cookie,
		ByteOffset:   offset,
		Size:         sizeWithZeros,
		Crc:          msgs.Crc(crc),
		StorageClass: storageClass,
		Blacklist:    blacklist,
		Parity:       spanPolicy.Parity,
		Stripes:      uint8(S),
		CellSize:     uint32(cellSize),
		Crcs:         make([]msgs.Crc, B*S),
	}

	if D == 1 { // mirroring
		for s := 0; s < S; s++ {
			crc := msgs.Crc(crc32c.Sum(0, (*data)[s*cellSize:(s+1)*cellSize]))
			for b := 0; b < B; b++ {
				initiateReq.Crcs[B*s+b] = crc
			}
		}
	} else { // RS
		// Make space for the parity blocks after the data blocks
		ensureLen(data, blockSize*B)
		rs := rs.Get(spanPolicy.Parity)
		dataSrcs := make([][]byte, D)
		parityDests := make([][]byte, P)
		for s := 0; s < S; s++ {
			// Compute CRCs for data blocks, and store their offsets
			for d := 0; d < D; d++ {
				dataStart := D*cellSize*s + cellSize*d
				dataEnd := D*cellSize*s + cellSize*(d+1)
				dataSrcs[d] = (*data)[dataStart:dataEnd]
				initiateReq.Crcs[B*s+d] = msgs.Crc(crc32c.Sum(0, dataSrcs[d]))
			}
			// Generate parity
			for p := 0; p < P; p++ {
				dataStart := S*D*cellSize + P*cellSize*s + cellSize*p
				dataEnd := S*D*cellSize + P*cellSize*s + cellSize*(p+1)
				parityDests[p] = (*data)[dataStart:dataEnd]
			}
			rs.ComputeParityInto(dataSrcs, parityDests)
			// Compute parity CRC
			for p := 0; p < P; p++ {
				initiateReq.Crcs[B*s+(D+p)] = msgs.Crc(crc32c.Sum(0, parityDests[p]))
			}
		}
	}

	return &initiateReq
}

func mkBlockReader(
	req *msgs.AddSpanInitiateReq,
	data []byte,
	block int,
) (msgs.Crc, io.Reader) {
	D := req.Parity.DataBlocks()
	P := req.Parity.ParityBlocks()
	B := req.Parity.Blocks()
	S := int(req.Stripes)
	cellSize := int(req.CellSize)
	blockCrc := uint32(0)
	for s := 0; s < int(req.Stripes); s++ {
		blockCrc = crc32c.Append(blockCrc, uint32(req.Crcs[B*s+block]), cellSize)
	}
	if D == 1 {
		// mirroring, we only have one block
		return msgs.Crc(blockCrc), bytes.NewReader(data)
	} else if block < D {
		// data block, first section of the blob
		r := &blockReader{
			cells:    S,
			cellSize: cellSize,
			stride:   cellSize * D,
			data:     data[block*cellSize:],
			cursor:   0,
		}
		return msgs.Crc(blockCrc), r
	} else {
		// parity block, second section of the blob
		r := &blockReader{
			cells:    S,
			cellSize: cellSize,
			stride:   cellSize * P,
			data:     data[D*S*cellSize+(block-D)*cellSize:],
			cursor:   0,
		}
		return msgs.Crc(blockCrc), r
	}
}

// Appends a new span to a given file.
// Note: the buffer underlying data might be modified by adding padding zeros
// for the purpose of splitting things into blocks/stripes. The (possibly modified)
// buffer is returned, regardless of whether the error is nil or not.
func (c *Client) CreateSpan(
	log *Logger,
	blacklist []msgs.BlacklistEntry,
	spanPolicies *msgs.SpanPolicy,
	blockPolicies *msgs.BlockPolicy,
	stripePolicy *msgs.StripePolicy,
	id msgs.InodeId,
	cookie [8]byte,
	offset uint64,
	// The span size might be greater than `len(*data)`, in which case we have trailing
	// zeros (this allows us to cheaply stored zero sections).
	spanSize uint32,
	// The contents of this pointer might be modified by this function (we might have to extend the
	// buffer), the intention is that if you're using a buf pool to get this you can put it back after.
	data *[]byte,
) error {
	log.Debug("writing span spanSize=%v len=%v", spanSize, len(*data))

	if len(*data) < 256 {
		if err := c.createInlineSpan(log, id, cookie, offset, spanSize, *data); err != nil {
			return err
		}
		return nil
	}

	// initiate span add
	var initiateReq *msgs.AddSpanInitiateReq
	initiateReq = prepareSpanInitiateReq(append([]msgs.BlacklistEntry{}, blacklist...), spanPolicies, blockPolicies, stripePolicy, id, cookie, offset, spanSize, data)
	{
		expectedSize := float64(spanSize) * float64(initiateReq.Parity.Blocks()) / float64(initiateReq.Parity.DataBlocks())
		actualSize := initiateReq.CellSize * uint32(initiateReq.Stripes) * uint32(initiateReq.Parity.Blocks())
		log.Debug("span logical size: %v, span physical size: %v, waste: %v%%", spanSize, actualSize, 100.0*(float64(actualSize)-expectedSize)/float64(actualSize))
	}

	maxAttempts := 5 // 4 = number of block services that can be down at once in the tests right now
	var err error
	for attempt := 0; ; attempt++ {
		log.Debug("span writing attempt %v", attempt+1)
		initiateResp := msgs.AddSpanInitiateResp{}
		if err = c.ShardRequest(log, id.Shard(), initiateReq, &initiateResp); err != nil {
			return err
		}
		// write blocks
		certifyReq := msgs.AddSpanCertifyReq{
			FileId:     id,
			Cookie:     cookie,
			ByteOffset: offset,
			Proofs:     make([]msgs.BlockProof, len(initiateResp.Blocks)),
		}
		for i, block := range initiateResp.Blocks {
			var conn BlocksConn
			conn, err = c.GetWriteBlocksConn(log, block.BlockServiceId, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2)
			if err != nil {
				initiateReq.Blacklist = append(initiateReq.Blacklist, msgs.BlacklistEntry{FailureDomain: block.BlockServiceFailureDomain})
				log.Info("failed to get blocks conn to %+v: %v, might retry without failure domain %q", block, err, string(block.BlockServiceFailureDomain.Name[:]))
				goto FailedAttempt
			}
			defer conn.Close()
			blockCrc, blockReader := mkBlockReader(initiateReq, *data, i)
			var proof [8]byte
			proof, err = WriteBlock(log, conn, &block, blockReader, initiateReq.CellSize*uint32(initiateReq.Stripes), blockCrc)
			if err != nil {
				log.Info("failed to write block to %+v: %v, might retry without failure domain %q", block, err, string(block.BlockServiceFailureDomain.Name[:]))
				initiateReq.Blacklist = append(initiateReq.Blacklist, msgs.BlacklistEntry{FailureDomain: block.BlockServiceFailureDomain})
				goto FailedAttempt
			} else {
				conn.Put()
			}
			certifyReq.Proofs[i].BlockId = block.BlockId
			certifyReq.Proofs[i].Proof = proof
		}
		if err = c.ShardRequest(log, id.Shard(), &certifyReq, &msgs.AddSpanCertifyResp{}); err != nil {
			return err
		}
		// we've managed
		break

	FailedAttempt:
		if attempt >= maxAttempts { // too many failures
			break
		}
		err = nil
		// create temp file, move the bad span there, then we can restart
		constructResp := &msgs.ConstructFileResp{}
		if err := c.ShardRequest(log, id.Shard(), &msgs.ConstructFileReq{Type: msgs.FILE, Note: "bad_add_span_attempt"}, constructResp); err != nil {
			return err
		}
		moveSpanReq := &msgs.MoveSpanReq{
			FileId1:     id,
			ByteOffset1: offset,
			Cookie1:     cookie,
			FileId2:     constructResp.Id,
			ByteOffset2: 0,
			Cookie2:     constructResp.Cookie,
			SpanSize:    spanSize,
		}
		if err := c.ShardRequest(log, id.Shard(), moveSpanReq, &msgs.MoveSpanResp{}); err != nil {
			return err
		}
	}

	return err
}

func (c *Client) WriteFile(
	log *Logger,
	bufPool *BufPool,
	dirInfoCache *DirInfoCache,
	dirId msgs.InodeId, // to get policies
	fileId msgs.InodeId,
	cookie [8]byte,
	r io.Reader,
) error {
	spanPolicies := msgs.SpanPolicy{}
	if _, err := c.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, &spanPolicies); err != nil {
		return err
	}
	blockPolicies := msgs.BlockPolicy{}
	if _, err := c.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, &blockPolicies); err != nil {
		return err
	}
	stripePolicy := msgs.StripePolicy{}
	if _, err := c.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, &stripePolicy); err != nil {
		return err
	}
	maxSpanSize := spanPolicies.Entries[len(spanPolicies.Entries)-1].MaxSize
	spanBuf := bufPool.Get(int(maxSpanSize))
	defer bufPool.Put(spanBuf)
	offset := uint64(0)
	for {
		*spanBuf = (*spanBuf)[:maxSpanSize]
		read, err := io.ReadFull(r, *spanBuf)
		if err != nil && err != io.EOF && err != io.ErrUnexpectedEOF {
			return err
		}
		if err == io.EOF {
			break
		}
		*spanBuf = (*spanBuf)[:read]
		err = c.CreateSpan(
			log, []msgs.BlacklistEntry{}, &spanPolicies, &blockPolicies, &stripePolicy,
			fileId, cookie, offset, uint32(read), spanBuf,
		)
		if err != nil {
			return err
		}
		offset += uint64(read)
		if read < int(maxSpanSize) {
			break
		}
	}
	return nil
}

func (c *Client) CreateFile(
	log *Logger,
	bufPool *BufPool,
	dirInfoCache *DirInfoCache,
	path string, // must be absolute
	r io.Reader,
) (msgs.InodeId, error) {
	if path[0] != '/' {
		return 0, fmt.Errorf("non-absolute file path %v", path)
	}
	dirPath := filepath.Dir(path)
	fileName := filepath.Base(path)
	if fileName == dirPath {
		return 0, fmt.Errorf("bad file path %v", path)
	}
	dirId, err := c.ResolvePath(log, dirPath)
	if err != nil {
		return 0, err
	}
	fileResp := msgs.ConstructFileResp{}
	if err := c.ShardRequest(log, dirId.Shard(), &msgs.ConstructFileReq{Type: msgs.FILE}, &fileResp); err != nil {
		return 0, err
	}
	fileId := fileResp.Id
	cookie := fileResp.Cookie
	if err := c.WriteFile(log, bufPool, dirInfoCache, dirId, fileId, cookie, r); err != nil {
		return 0, err
	}
	if err := c.ShardRequest(log, dirId.Shard(), &msgs.LinkFileReq{FileId: fileId, Cookie: cookie, OwnerId: dirId, Name: fileName}, &msgs.LinkFileResp{}); err != nil {
		return 0, err
	}
	return fileId, nil
}

type FetchedStripe struct {
	Buf   *[]byte
	Start uint64
	owned bool
}

func (fs *FetchedStripe) Put(bufPool *BufPool) {
	if !fs.owned {
		return
	}
	fs.owned = false
	bufPool.Put(fs.Buf)
}

func (c *Client) fetchCell(
	log *Logger,
	bufPool *BufPool,
	blockServices []msgs.BlockService,
	span *msgs.FetchedSpan,
	body *msgs.FetchedBlocksSpan,
	blockIx uint8,
	cell uint8,
) (buf *[]byte, err error) {
	buf = bufPool.Get(int(body.CellSize))
	defer func() {
		if err != nil {
			bufPool.Put(buf)
		}
	}()
	block := &body.Blocks[blockIx]
	blockService := &blockServices[block.BlockServiceIx]
	var conn BlocksConn
	conn, err = c.GetReadBlocksConn(log, blockService.Id, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2)
	if err != nil {
		log.Info("could not connect to block service %+v", blockService)
		return nil, err
	}
	defer conn.Close()
	err = FetchBlock(log, conn, blockService, block.BlockId, uint32(cell)*body.CellSize, body.CellSize)
	if err != nil {
		log.Info("could not fetch block from block service %+v: %+v", blockService, err)
		return nil, err
	}
	cursor := 0
	for cursor < int(body.CellSize) {
		var read int
		read, err = conn.Read((*buf)[cursor:])
		if err != nil {
			return nil, err
		}
		cursor += read
	}
	conn.Put()
	return buf, nil
}

func (c *Client) fetchMirroredStripe(
	log *Logger,
	bufPool *BufPool,
	blockServices []msgs.BlockService,
	span *msgs.FetchedSpan,
	body *msgs.FetchedBlocksSpan,
	offset uint64,
) (start uint64, buf *[]byte, err error) {
	spanOffset := uint32(offset - span.Header.ByteOffset)
	cell := spanOffset / body.CellSize
	B := body.Parity.Blocks()
	start = span.Header.ByteOffset + uint64(cell*body.CellSize)

	log.Debug("getting cell %v -> %v", start, span.Header.ByteOffset+uint64((cell+1)*body.CellSize))

	found := false
	for i := 0; i < B && !found; i++ {
		block := &body.Blocks[i]
		blockService := &blockServices[block.BlockServiceIx]
		buf, err = c.fetchCell(log, bufPool, blockServices, span, body, uint8(i), uint8(cell))
		if err != nil {
			continue
		}
		crc := msgs.Crc(crc32c.Sum(0, *buf))
		if crc != body.StripesCrc[cell] {
			log.RaiseAlert("expected crc %v, got %v, for block %v in block service %v", body.StripesCrc[cell], crc, block.BlockId, blockService.Id)
			continue
		}
		found = true
	}

	if !found {
		return 0, nil, fmt.Errorf("could not find any suitable blocks")
	}

	return start, buf, nil
}

func (c *Client) fetchRsStripe(
	log *Logger,
	bufPool *BufPool,
	blockServices []msgs.BlockService,
	span *msgs.FetchedSpan,
	body *msgs.FetchedBlocksSpan,
	offset uint64,
) (start uint64, buf *[]byte, err error) {
	D := body.Parity.DataBlocks()
	B := body.Parity.Blocks()
	spanOffset := uint32(offset - span.Header.ByteOffset)
	blocks := make([]*[]byte, B)
	defer func() {
		for i := range blocks {
			bufPool.Put(blocks[i])
		}
	}()
	blocksFound := 0
	stripe := spanOffset / (uint32(D) * body.CellSize)
	if stripe >= uint32(body.Stripes) {
		panic(fmt.Errorf("impossible: stripe %v >= stripes %v, spanOffset=%v, spanSize=%v, cellSize=%v, D=%v", stripe, body.Stripes, spanOffset, span.Header.Size, body.CellSize, D))
	}
	log.Debug("fetching stripe %v, cell size %v", stripe, body.CellSize)
	for i := 0; i < B; i++ {
		buf, err = c.fetchCell(log, bufPool, blockServices, span, body, uint8(i), uint8(stripe))
		if err != nil {
			continue
		}
		// we managed to get the block, store it
		blocks[i] = buf
		blocksFound++
		if blocksFound >= D {
			break
		}
	}
	if blocksFound != D {
		return 0, nil, fmt.Errorf("couldn't get enough block connections (need at least %v, got %v)", D, blocksFound)
	}

	stripeBuf := bufPool.Get(D * int(body.CellSize))

	// check if we're missing data blocks, and recover them if needed
	stripeCrc := uint32(0)
	for i := 0; i < D; i++ {
		if blocks[i] == nil {
			haveBlocksRecover := [][]byte{}
			haveBlocksRecoverIxs := []uint8{}
			for j := 0; j < B; j++ {
				if blocks[j] != nil {
					haveBlocksRecover = append(haveBlocksRecover, *blocks[j])
					haveBlocksRecoverIxs = append(haveBlocksRecoverIxs, uint8(j))
					if len(haveBlocksRecover) >= D {
						break
					}
				}
			}
			blocks[i] = bufPool.Get(int(body.CellSize))
			rs.Get(body.Parity).RecoverInto(haveBlocksRecoverIxs, haveBlocksRecover, uint8(i), *blocks[i])
		}
		stripeCrc = crc32c.Sum(stripeCrc, *blocks[i])
		copy((*stripeBuf)[i*int(body.CellSize):(i+1)*int(body.CellSize)], *blocks[i])
	}

	// check if our crc is scuppered
	if stripeCrc != uint32(body.StripesCrc[stripe]) {
		// TODO implement corrupted stripe repair
		return 0, nil, fmt.Errorf("bad crc, expected %v, got %v", msgs.Crc(stripeCrc), body.StripesCrc[stripe])
	}

	return span.Header.ByteOffset + uint64(stripe)*uint64(D)*uint64(body.CellSize), stripeBuf, nil
}

// Returns nil, nil if span or stripe cannot be found.
// Stripe might not be found because
func (c *Client) FetchStripe(
	log *Logger,
	bufPool *BufPool,
	blockServices []msgs.BlockService,
	spans []msgs.FetchedSpan,
	offset uint64,
) (*FetchedStripe, error) {
	// find span
	spanIx := sort.Search(len(spans), func(i int) bool {
		return offset < spans[i].Header.ByteOffset+uint64(spans[i].Header.Size)
	})
	if spanIx >= len(spans) {
		log.Debug("empty file offset=%v spanIx=%v len(spans)=%v", offset, spanIx, len(spans))
		return nil, nil // out of spans
	}
	span := &spans[spanIx]
	if offset >= (span.Header.ByteOffset + uint64(span.Header.Size)) {
		log.Debug("could not find span")
		return nil, nil // out of spans
	}

	log.DebugStack(1, "will fetch span %v -> %v", span.Header.ByteOffset, span.Header.ByteOffset+uint64(span.Header.Size))

	// if inline, it's very easy
	if span.Header.StorageClass == msgs.INLINE_STORAGE {
		data := span.Body.(*msgs.FetchedInlineSpan).Body
		dataCrc := msgs.Crc(crc32c.Sum(0, data))
		if dataCrc != span.Header.Crc {
			panic(fmt.Errorf("header CRC for inline span is %v, but data is %v", span.Header.Crc, dataCrc))
		}
		stripe := &FetchedStripe{
			Buf:   &span.Body.(*msgs.FetchedInlineSpan).Body,
			Start: span.Header.ByteOffset,
		}
		log.Debug("fetched inline span")
		return stripe, nil
	}

	// otherwise we need to fetch
	body := span.Body.(*msgs.FetchedBlocksSpan)
	D := body.Parity.DataBlocks()

	// if we're in trailing zeros, just return the trailing zeros part
	// (this is a "synthetic" stripe of sorts, but it's helpful to callers)
	spanDataEnd := span.Header.ByteOffset + uint64(body.Stripes)*uint64(D*int(body.CellSize))
	if offset >= spanDataEnd {
		buf := bufPool.Get(int(uint64(span.Header.Size) - spanDataEnd))
		for i := range *buf {
			(*buf)[i] = 0
		}
		stripe := &FetchedStripe{
			Buf:   buf,
			Start: spanDataEnd,
			owned: true,
		}
		return stripe, nil
	}

	// otherwise just fetch
	var start uint64
	var buf *[]byte
	var err error
	if D == 1 {
		start, buf, err = c.fetchMirroredStripe(log, bufPool, blockServices, span, body, offset)
		if err != nil {
			return nil, err
		}
	} else {
		start, buf, err = c.fetchRsStripe(log, bufPool, blockServices, span, body, offset)
		if err != nil {
			return nil, err
		}
	}

	// chop off trailing zeros in the span
	stripeEnd := start + uint64(len(*buf))
	spanEnd := span.Header.ByteOffset + uint64(span.Header.Size)
	if stripeEnd > spanEnd {
		*buf = (*buf)[:uint64(len(*buf))-(stripeEnd-spanEnd)]
	}

	// we're done
	stripe := &FetchedStripe{
		Buf:   buf,
		owned: true,
		Start: start,
	}
	return stripe, nil
}

func (c *Client) FetchSpans(
	log *Logger,
	fileId msgs.InodeId,
) (blockServices []msgs.BlockService, spans []msgs.FetchedSpan, err error) {
	req := msgs.FileSpansReq{FileId: fileId}
	resp := msgs.FileSpansResp{}
	for {
		if err := c.ShardRequest(log, fileId.Shard(), &req, &resp); err != nil {
			return nil, nil, err
		}
		for s := range resp.Spans {
			span := &resp.Spans[s]
			body, hasBlock := span.Body.(*msgs.FetchedBlocksSpan)
			// adjust indices
			if hasBlock {
				for b := range body.Blocks {
					block := &body.Blocks[b]
					blockService := &resp.BlockServices[block.BlockServiceIx]
					found := false
					for bs := range blockServices {
						if blockServices[bs].Id == blockService.Id {
							block.BlockServiceIx = uint8(bs)
							found = true
						}
					}
					if !found {
						blockServices = append(blockServices, *blockService)
						if len(blockServices) > 266 {
							panic(fmt.Errorf("too many block services"))
						}
						block.BlockServiceIx = uint8(len(blockServices) - 1)
					}
				}
			}
			spans = append(spans, *span)
		}
		req.ByteOffset = resp.NextOffset
		if req.ByteOffset == 0 {
			break
		}
	}
	log.Debug("fetched bss %+v spans %+v", blockServices, spans)
	return blockServices, spans, err
}

type fileReader struct {
	client        *Client
	log           *Logger
	bufPool       *BufPool
	fileId        msgs.InodeId
	blockServices []msgs.BlockService
	spans         []msgs.FetchedSpan
	currentStripe *FetchedStripe
	cursor        uint64
}

func (f *fileReader) Close() error {
	if f.currentStripe != nil {
		f.currentStripe.Put(f.bufPool)
	}
	return nil
}

func (f *fileReader) Read(p []byte) (int, error) {
	if f.currentStripe == nil || f.cursor >= (f.currentStripe.Start+uint64(len(*f.currentStripe.Buf))) {
		var err error
		f.currentStripe, err = f.client.FetchStripe(f.log, f.bufPool, f.blockServices, f.spans, f.cursor)
		if err != nil {
			return 0, err
		}
		if f.currentStripe == nil {
			return 0, io.EOF
		}
		return f.Read(p)
	}
	r := copy(p, (*f.currentStripe.Buf)[f.cursor-f.currentStripe.Start:])
	f.cursor += uint64(r)
	return r, nil
}

func (c *Client) ReadFile(
	log *Logger,
	bufPool *BufPool,
	id msgs.InodeId,
) (io.ReadCloser, error) {
	blockServices, spans, err := c.FetchSpans(log, id)
	if err != nil {
		return nil, err
	}
	r := &fileReader{
		client:        c,
		log:           log,
		bufPool:       bufPool,
		fileId:        id,
		blockServices: blockServices,
		spans:         spans,
	}
	return r, nil
}
