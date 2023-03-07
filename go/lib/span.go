package lib

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"sync"
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

func ensureLen(buf []byte, l int) []byte {
	lenBefore := len(buf)
	if l <= cap(buf) {
		buf = buf[:l]
	} else {
		buf = buf[:cap(buf)]
		buf = append(buf, make([]byte, l-len(buf))...)
	}
	// memset? what's that?
	for i := lenBefore; i < len(buf); i++ {
		buf[i] = 0
	}
	return buf
}

func prepareSpanInitiateReq(
	blacklist []msgs.BlockServiceId,
	spanPolicies *msgs.SpanPolicy,
	blockPolicies *msgs.BlockPolicy,
	stripePolicy *msgs.StripePolicy,
	id msgs.InodeId,
	cookie [8]byte,
	offset uint64,
	sizeWithZeros uint32,
	data []byte,
) ([]byte, *msgs.AddSpanInitiateReq) {
	if int(sizeWithZeros) < len(data) {
		panic(fmt.Errorf("sizeWithZeros=%v < len(data)=%v", sizeWithZeros, len(data)))
	}

	crc := crc32c.Sum(0, data)
	crc = crc32c.ZeroExtend(crc, int(sizeWithZeros)-len(data))

	// Compute all the size parameters. We use TargetStripeSize as an upper bound,
	// for now (i.e. the stripe will always be smaller than TargetStripeSize)
	S := (len(data) + int(stripePolicy.TargetStripeSize) - 1) / int(stripePolicy.TargetStripeSize)
	if S > 15 {
		S = 15
	}
	spanPolicy := spanPolicies.Pick(uint32(len(data)))
	D := spanPolicy.Parity.DataBlocks()
	P := spanPolicy.Parity.ParityBlocks()
	B := spanPolicy.Parity.Blocks()
	blockSize := (len(data) + D - 1) / D
	cellSize := (blockSize + S - 1) / S
	blockSize = cellSize * S
	storageClass := blockPolicies.Pick(uint32(blockSize)).StorageClass

	// Pad the data with zeros
	data = ensureLen(data, S*D*cellSize)

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
			crc := msgs.Crc(crc32c.Sum(0, data[s*cellSize:(s+1)*cellSize]))
			for b := 0; b < B; b++ {
				initiateReq.Crcs[B*s+b] = crc
			}
		}
	} else { // RS
		// Make space for the parity blocks after the data blocks
		data = ensureLen(data, blockSize*B)
		rs := rs.Get(spanPolicy.Parity)
		dataSrcs := make([][]byte, D)
		parityDests := make([][]byte, P)
		for s := 0; s < S; s++ {
			// Compute CRCs for data blocks, and store their offsets
			for d := 0; d < D; d++ {
				dataStart := D*cellSize*s + cellSize*d
				dataEnd := D*cellSize*s + cellSize*(d+1)
				dataSrcs[d] = data[dataStart:dataEnd]
				initiateReq.Crcs[B*s+d] = msgs.Crc(crc32c.Sum(0, dataSrcs[d]))
			}
			// Generate parity
			for p := 0; p < P; p++ {
				dataStart := S*D*cellSize + P*cellSize*s + cellSize*p
				dataEnd := S*D*cellSize + P*cellSize*s + cellSize*(p+1)
				parityDests[p] = data[dataStart:dataEnd]
			}
			rs.ComputeParityInto(dataSrcs, parityDests)
			// Compute parity CRC
			for p := 0; p < P; p++ {
				initiateReq.Crcs[B*s+(D+p)] = msgs.Crc(crc32c.Sum(0, parityDests[p]))
			}
		}
	}

	return data, &initiateReq
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
	blacklist []msgs.BlockServiceId,
	spanPolicies *msgs.SpanPolicy,
	blockPolicies *msgs.BlockPolicy,
	stripePolicy *msgs.StripePolicy,
	id msgs.InodeId,
	cookie [8]byte,
	offset uint64,
	// The span size might be greater than `len(*data)`, in which case we have trailing
	// zeros (this allows us to cheaply stored zero sections).
	spanSize uint32,
	// This function might append to this, and write after it. It never modifies the data.
	// The new buffer is returned, so the caller can keep using the new, larger buffer,
	// for subsequent spans.
	//
	// Note that the new buffer is returned even if an error is returned.
	data []byte,
) ([]byte, error) {
	if len(data) < 256 {
		if err := c.createInlineSpan(log, id, cookie, offset, spanSize, data); err != nil {
			return data, err
		}
		return data, nil
	}

	var initiateReq *msgs.AddSpanInitiateReq
	data, initiateReq = prepareSpanInitiateReq(blacklist, spanPolicies, blockPolicies, stripePolicy, id, cookie, offset, spanSize, data)
	{
		expectedSize := float64(spanSize) * float64(initiateReq.Parity.Blocks()/initiateReq.Parity.DataBlocks())
		actualSize := initiateReq.CellSize * uint32(initiateReq.Stripes) * uint32(initiateReq.Parity.Blocks())
		log.Debug("span logical size: %v, span physical size: %v, waste: %v%%", spanSize, actualSize, 100.0*(float64(actualSize)-expectedSize)/float64(actualSize))
	}

	initiateResp := msgs.AddSpanInitiateResp{}
	if err := c.ShardRequest(log, id.Shard(), initiateReq, &initiateResp); err != nil {
		return data, err
	}

	certifyReq := msgs.AddSpanCertifyReq{
		FileId:     id,
		Cookie:     cookie,
		ByteOffset: offset,
		Proofs:     make([]msgs.BlockProof, len(initiateResp.Blocks)),
	}
	for i, block := range initiateResp.Blocks {
		conn, err := c.GetBlocksConn(log, block.BlockServiceId, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2)
		if err != nil {
			return data, err
		}
		blockCrc, blockReader := mkBlockReader(initiateReq, data, i)
		proof, err := WriteBlock(log, conn, &block, blockReader, initiateReq.CellSize*uint32(initiateReq.Stripes), blockCrc)
		conn.Close()
		if err != nil {
			return data, fmt.Errorf("writing block failed with error %v", err)
		}
		certifyReq.Proofs[i].BlockId = block.BlockId
		certifyReq.Proofs[i].Proof = proof
	}
	if err := c.ShardRequest(log, id.Shard(), &certifyReq, &msgs.AddSpanCertifyResp{}); err != nil {
		return data, err
	}

	return data, nil
}

type mirroredSpanReader struct {
	cursor    int
	block     int
	blockConn io.ReadCloser
	cellBuf   *[]byte
	cellCrcs  []msgs.Crc // starting from the _next_ stripe crc
}

func (r *mirroredSpanReader) Close() error {
	return r.blockConn.Close()
}

type rsNormalSpanReader struct {
	bufPool           *ReadSpanBufPool
	cursor            int
	haveBlocks        []uint8 // which blocks are we fetching. most of the times it'll just be the data blocks
	blockConns        []io.ReadCloser
	blocksRunningCrcs []msgs.Crc
	stripeBuf         *[]byte
	stripeCrcs        []msgs.Crc // starting from the _next_ stripe CRC.
	parityBuffers     []*[]byte  // buffers in which to temporarily place the parity blocks. usually empty
}

func (sr *rsNormalSpanReader) Close() error {
	sr.bufPool.put(sr.stripeBuf)
	for _, b := range sr.parityBuffers {
		sr.bufPool.put(b)
	}
	var lastErr error
	for _, c := range sr.blockConns {
		if err := c.Close(); err != nil {
			fmt.Fprintf(os.Stderr, "got error when closing connection: %v\n", err)
			lastErr = err
		}
	}
	return lastErr
}

// This is when we actively detect a bad CRC, and we have no choice but to load
// the remainder of the span in its entirety to find out which block is broken,
// and then resume.
type rsCorruptedSpanReader struct {
	bufPool     *ReadSpanBufPool
	startStripe int // at which stripe we switched to said reading
	cursor      int
	dataBlocks  []*[]byte
}

func (r *rsCorruptedSpanReader) Close() error {
	for _, b := range r.dataBlocks {
		r.bufPool.put(b)
	}
	return nil
}

type spanReader struct {
	bufPool    *ReadSpanBufPool
	spanSize   uint32
	spanCrc    msgs.Crc
	readBytes  uint32
	runningCrc msgs.Crc
	parity     rs.Parity
	stripes    uint8
	cellSize   uint32
	blocksCrcs []msgs.Crc
	blockConn  func(block int, offset uint32, size uint32) (io.ReadCloser, error)
	r          io.Closer
}

func (sr *spanReader) Close() error {
	return sr.r.Close()
}

func (sr *spanReader) repairCorruptedStripe(
	bufPool *ReadSpanBufPool,
	// the broken stripe
	stripe int,
	stripeData *[]byte,
	parityData []*[]byte,
	haveBlocks []uint8, // the blocks we have been using so far
	haveBlocksCrc []msgs.Crc, // the CRCs so far for the blocks that we have
	haveBlocksConns []io.ReadCloser, // the connections to the blocks we already have
) (*rsCorruptedSpanReader, error) {
	D := sr.parity.DataBlocks()
	B := sr.parity.Blocks()
	blockStart := stripe * int(sr.cellSize)
	remainingBlockSize := (int(sr.stripes) - stripe) * int(sr.cellSize)
	goodBlocks := make([]bool, B) // _known_ good blocks
	badBlocks := make([]bool, B)  // _known_ bad blocks
	blocksData := make([]*[]byte, B)
	defer func() {
		for i := D; i < B; i++ {
			b := blocksData[i]
			if b != nil {
				bufPool.put(b)
			}
		}
	}()
	// make space for full sized remaining data blocks
	for b := 0; b < D; b++ {
		blocksData[b] = bufPool.get(remainingBlockSize)
	}
	// load current stripe data into the blocks buffers
	{
		parityIx := 0
		for _, b := range haveBlocks {
			if int(b) < D {
				copy(*blocksData[int(b)], (*stripeData)[int(b)*int(sr.cellSize):])
			} else {
				blocksData[b] = bufPool.get(remainingBlockSize)
				copy(*blocksData[int(b)], *parityData[parityIx])
				parityIx++
			}
		}
	}
	// load _rest_ of blocks, check which ones are good
	numGoodBlocks := 0
	for {
		cursors := make([]int, D)
		for i := range cursors {
			cursors[i] = int(sr.cellSize)
		}
		allDone := true
		for i, b := range haveBlocks {
			read, err := haveBlocksConns[i].Read((*blocksData[int(b)])[cursors[i]:])
			if err != io.EOF && err != nil {
				return nil, err
			}
			haveBlocksCrc[i] = msgs.Crc(crc32c.Sum(uint32(haveBlocksCrc[i]), (*blocksData[int(b)])[cursors[i]:cursors[i]+read]))
			cursors[i] += read
			if cursors[i] < len(*blocksData[int(b)]) {
				allDone = false
			} else if msgs.Crc(haveBlocksCrc[i]) != sr.blocksCrcs[b] {
				badBlocks[int(b)] = true
			} else {
				goodBlocks[int(b)] = true
				numGoodBlocks++
			}
		}
		if allDone {
			break
		}
	}
	// Find and download blocks to recover
	var tmpBuf *[]byte
	defer bufPool.put(tmpBuf)
	for b := 0; b < B && numGoodBlocks < D; b++ {
		if badBlocks[b] || goodBlocks[b] { // we know this is bad
			continue
		}
		// We can try to download this one.
		// We need to download the entire blocks here, because we need to check the CRC.
		blockSize := sr.cellSize * uint32(sr.stripes)
		conn, err := sr.blockConn(b, 0, blockSize)
		if err != nil {
			return nil, err
		}
		if conn == nil {
			badBlocks[b] = true
			continue
		}
		if tmpBuf == nil {
			tmpBuf = bufPool.get(int(blockSize))
		}
		if _, err := io.ReadFull(conn, *tmpBuf); err != nil {
			conn.Close()
			return nil, err
		}
		if err := conn.Close(); err != nil {
			return nil, err
		}
		crc := crc32c.Sum(0, *tmpBuf)
		if crc == uint32(sr.blocksCrcs[b]) {
			// this is a good one
			blocksData[b] = bufPool.get(remainingBlockSize)
			copy(*blocksData[b], (*tmpBuf)[blockStart:])
			goodBlocks[b] = true
			numGoodBlocks++
		} else {
			badBlocks[b] = true
		}
	}
	if numGoodBlocks < D {
		return nil, fmt.Errorf("the number of good blocks (%v) is lower than the number of data blocks (%v)", numGoodBlocks, D)
	}
	newHaveBlocks := []uint8{}
	newHaveBlocksData := [][]byte{}
	for b := 0; b < B; b++ {
		if !goodBlocks[b] {
			continue
		}
		newHaveBlocks = append(newHaveBlocks, uint8(b))
		newHaveBlocksData = append(newHaveBlocksData, *blocksData[b])
	}
	rs := rs.Get(sr.parity)
	for b := 0; b < D; b++ {
		if goodBlocks[b] {
			continue
		}
		rs.RecoverInto(newHaveBlocks, newHaveBlocksData, uint8(b), *blocksData[b])
	}
	r := rsCorruptedSpanReader{
		bufPool:     bufPool,
		startStripe: stripe,
		cursor:      stripe * D * int(sr.cellSize),
		dataBlocks:  blocksData[:D],
	}
	return &r, nil
}

func (sr *spanReader) loadStripe(nsr *rsNormalSpanReader) error {
	D := sr.parity.DataBlocks()
	blocksCursors := make([]int, len(nsr.blockConns))
	needsRecover := false
	for {
		allDone := true
		parityIx := 0
		for i, b := range nsr.haveBlocks {
			var buf []byte
			if int(b) < D { // data block, already ready
				stripeStart := int(b)*int(sr.cellSize) + blocksCursors[b]
				stripeEnd := (int(b) + 1) * int(sr.cellSize)
				buf = (*nsr.stripeBuf)[stripeStart:stripeEnd]
			} else { // parity block
				needsRecover = true
				buf = (*nsr.parityBuffers[parityIx])[blocksCursors[i]:]
				parityIx++
			}
			blockRead, err := nsr.blockConns[i].Read(buf)
			if err != nil {
				return err
			}
			nsr.blocksRunningCrcs[i] = msgs.Crc(crc32c.Sum(uint32(nsr.blocksRunningCrcs[i]), buf[:blockRead]))
			blocksCursors[i] += blockRead
			if blocksCursors[i] < int(sr.cellSize) {
				allDone = false
			}
		}
		if allDone {
			break
		}
	}
	if needsRecover { // we need to recover data blocks
		rs := rs.Get(sr.parity)
		bufs := make([][]byte, D)
		parityIx := 0
		for i, b := range nsr.haveBlocks {
			if int(b) < D {
				stripeStart := int(b) * int(sr.cellSize)
				stripeEnd := (int(b) + 1) * int(sr.cellSize)
				bufs[i] = (*nsr.stripeBuf)[stripeStart:stripeEnd]
			} else {
				bufs[i] = *nsr.parityBuffers[parityIx]
				parityIx++
			}
		}
		lastDataBlock := uint8(0)
		for i := 0; i < D; i++ {
			if int(nsr.haveBlocks[i]) >= D {
				break
			}
			for ; lastDataBlock < nsr.haveBlocks[i]; lastDataBlock++ {
				stripeStart := int(lastDataBlock) * int(sr.cellSize)
				stripeEnd := (int(lastDataBlock) + 1) * int(sr.cellSize)
				rs.RecoverInto(nsr.haveBlocks, bufs, lastDataBlock, (*nsr.stripeBuf)[stripeStart:stripeEnd])
			}
			lastDataBlock++
		}
	}
	crc := crc32c.Sum(0, *nsr.stripeBuf)
	if crc != uint32(nsr.stripeCrcs[0]) {
		var err error
		sr.r, err = sr.repairCorruptedStripe(
			sr.bufPool,
			nsr.cursor/(int(sr.cellSize)*D),
			nsr.stripeBuf,
			nsr.parityBuffers,
			nsr.haveBlocks,
			nsr.blocksRunningCrcs,
			nsr.blockConns,
		)
		nsr.Close() // close connections
		if err != nil {
			return err
		}
		return nil
	}
	nsr.stripeCrcs = nsr.stripeCrcs[1:]
	return nil
}

func (sr *spanReader) loadCell(mr *mirroredSpanReader) error {
	l := func() (bool, error) {
		cursor := 0
		for cursor < int(sr.cellSize) {
			read, err := mr.blockConn.Read((*mr.cellBuf)[cursor:])
			if err != nil {
				return false, err
			}
			cursor += read
		}
		crc := crc32c.Sum(0, *mr.cellBuf)
		good := crc == uint32(mr.cellCrcs[0])
		if !good {
			if err := mr.blockConn.Close(); err != nil {
				return false, err
			}
		}
		return good, nil
	}
	good, err := l()
	if err != nil {
		return err
	}
	// look for another block if necessary
	startingBlock := mr.block
	B := sr.parity.Blocks()
	for !good {
		mr.block = (mr.block + 1) % B
		if mr.block == startingBlock {
			break
		}
		var err error
		mr.blockConn, err = sr.blockConn(int(mr.block), uint32(mr.cursor), sr.cellSize*uint32(sr.stripes)-uint32(mr.cursor))
		if err != nil {
			return err
		}
		if mr.blockConn == nil {
			continue
		}
		good, err = l()
		if err != nil {
			return err
		}
	}
	if !good {
		return fmt.Errorf("could not find block without CRC errors")
	}
	mr.cellCrcs = mr.cellCrcs[1:]
	return nil
}

func (sr *spanReader) readMirrored(mr *mirroredSpanReader, p []byte) (int, error) {
	if mr.cursor >= int(sr.spanSize) {
		return 0, io.EOF
	}
	if mr.cursor >= int(sr.stripes)*int(sr.cellSize) { // trailing zeros
		read := 0
		for mr.cursor < int(sr.spanSize) && read < len(p) {
			p[read] = 0
			read++
			mr.cursor++
		}
		return read, nil
	}
	currentCell := mr.cursor / int(sr.cellSize)
	cellPos := mr.cursor % int(sr.cellSize)
	remainingCell := (*mr.cellBuf)[cellPos:]
	if mr.cursor+len(remainingCell) > int(sr.spanSize) { // zero padding
		remainingCell = remainingCell[:int(sr.spanSize)-mr.cursor]
	}
	read := copy(p, remainingCell)
	mr.cursor += read
	if cellPos+read == len(*mr.cellBuf) && currentCell+1 < int(sr.stripes) {
		if err := sr.loadCell(mr); err != nil {
			return read, err
		}
	}
	return read, nil
}

func (sr *spanReader) readRsHappy(nsr *rsNormalSpanReader, p []byte) (int, error) {
	if nsr.cursor >= int(sr.spanSize) {
		return 0, io.EOF
	}
	D := sr.parity.DataBlocks()
	if nsr.cursor >= int(sr.stripes)*D*int(sr.cellSize) { // trailing zeros
		read := 0
		for nsr.cursor < int(sr.spanSize) && read < len(p) {
			p[read] = 0
			read++
			nsr.cursor++
		}
		return read, nil
	}
	currentStripe := nsr.cursor / (D * int(sr.cellSize))
	stripePos := nsr.cursor % (D * int(sr.cellSize))
	remainingStripe := (*nsr.stripeBuf)[stripePos:]
	if nsr.cursor+len(remainingStripe) > int(sr.spanSize) { // zero padding
		remainingStripe = remainingStripe[:int(sr.spanSize)-nsr.cursor]
	}
	read := copy(p, remainingStripe)
	nsr.cursor += read
	if stripePos+read == len(*nsr.stripeBuf) && currentStripe+1 < int(sr.stripes) {
		if err := sr.loadStripe(nsr); err != nil {
			return read, err
		}
	}
	return read, nil
}

func (sr *spanReader) readRsCorrupted(wsr *rsCorruptedSpanReader, p []byte) (int, error) {
	D := sr.parity.DataBlocks()
	if wsr.cursor >= int(sr.spanSize) {
		return 0, io.EOF
	}
	if wsr.cursor >= int(sr.stripes)*D*int(sr.cellSize) { // trailing zeros
		read := 0
		for wsr.cursor < int(sr.spanSize) && read < len(p) {
			p[read] = 0
			read++
			wsr.cursor++
		}
		return read, nil
	}
	currentStripe := wsr.cursor / (D * int(sr.cellSize))
	stripePos := wsr.cursor % (D * int(sr.cellSize))
	currentBlock := stripePos / int(sr.cellSize)
	cellPos := stripePos % int(sr.cellSize)
	remainingCell := (*wsr.dataBlocks[currentBlock])[int(sr.cellSize)*(currentStripe-wsr.startStripe)+cellPos : int(sr.cellSize)*(currentStripe-wsr.startStripe+1)]
	if wsr.cursor+len(remainingCell) > int(sr.spanSize) { // zero padding
		remainingCell = remainingCell[:int(sr.spanSize)-wsr.cursor]
	}
	read := copy(p, remainingCell)
	wsr.cursor += read
	return read, nil
}

func (sr *spanReader) Read(p []byte) (int, error) {
	var read int
	var err error
	switch r := sr.r.(type) {
	case *mirroredSpanReader:
		read, err = sr.readMirrored(r, p)
	case *rsNormalSpanReader:
		read, err = sr.readRsHappy(r, p)
	case *rsCorruptedSpanReader:
		read, err = sr.readRsCorrupted(r, p)
	default:
		panic(fmt.Errorf("bad reader %T", sr.r))
	}
	sr.readBytes += uint32(read)
	if sr.readBytes > sr.spanSize {
		panic(fmt.Errorf("read beyond end of span -- %v vs %v", sr.readBytes, sr.spanSize))
	}
	sr.runningCrc = msgs.Crc(crc32c.Sum(uint32(sr.runningCrc), p[:read]))
	if sr.readBytes == sr.spanSize && sr.runningCrc != sr.spanCrc {
		panic(fmt.Errorf("span contents CRC is not what we expect -- %v vs %v", sr.runningCrc, sr.spanCrc))
	}
	return read, err
}

// Given a way to start streaming a block, produces a stream with the span
// contents. Will automatically repair the span if CRC errors are detected.
func readSpanFromBlocks(
	bufPool *ReadSpanBufPool,
	spanSize uint32,
	spanCrc msgs.Crc,
	parity rs.Parity,
	stripes uint8,
	cellSize uint32,
	blockCrcs []msgs.Crc,
	stripesCrc []msgs.Crc,
	// If this function returns `nil, nil`, it means that that block service
	// is currently not available for whatever reason.
	//
	// We currently make the assumption that the connections that are available
	// at the beginning will be available throughout the duration of span reading.
	blockConn func(block int, offset uint32, size uint32) (io.ReadCloser, error),
) (io.ReadCloser, error) {
	D := parity.DataBlocks()
	B := parity.Blocks()
	sr := spanReader{
		bufPool:    bufPool,
		spanSize:   spanSize,
		spanCrc:    spanCrc,
		stripes:    stripes,
		cellSize:   cellSize,
		parity:     parity,
		blockConn:  blockConn,
		blocksCrcs: blockCrcs,
	}
	if D == 1 {
		mr := &mirroredSpanReader{
			cursor:   0,
			cellBuf:  sr.bufPool.get(int(cellSize)),
			cellCrcs: stripesCrc,
		}
		for b := 0; b < B; b++ {
			conn, err := blockConn(b, 0, uint32(cellSize)*uint32(stripes))
			if err != nil {
				return nil, err
			}
			if conn != nil {
				mr.blockConn = conn
				mr.block = b
				break
			}
		}
		if mr.blockConn == nil {
			return nil, fmt.Errorf("couldn't get block connection to any of the blocks")
		}
		sr.r = mr
		if err := sr.loadCell(mr); err != nil {
			return nil, err
		}
	} else {
		conns := make([]io.ReadCloser, 0)
		haveBlocks := make([]uint8, 0)
		parityBuffers := []*[]byte{}
		for i := 0; i < B; i++ {
			conn, err := blockConn(i, 0, uint32(cellSize)*uint32(stripes))
			if err != nil {
				return nil, err
			}
			if conn == nil {
				continue
			}
			conns = append(conns, conn)
			haveBlocks = append(haveBlocks, uint8(i))
			if i >= D {
				parityBuffers = append(parityBuffers, sr.bufPool.get(int(cellSize)))
			}
			if len(haveBlocks) == D {
				break
			}
		}
		if len(haveBlocks) != D {
			return nil, fmt.Errorf("couldn't get enough block connections (need at least %v, got %v)", D, len(conns))
		}
		stripeBuf := sr.bufPool.get(int(cellSize) * D)
		rsr := &rsNormalSpanReader{
			bufPool:           sr.bufPool,
			cursor:            0,
			blockConns:        conns,
			haveBlocks:        haveBlocks,
			stripeBuf:         stripeBuf,
			stripeCrcs:        stripesCrc,
			blocksRunningCrcs: make([]msgs.Crc, D),
			parityBuffers:     parityBuffers,
		}
		sr.r = rsr
		if err := sr.loadStripe(rsr); err != nil {
			return nil, err
		}
	}
	return &sr, nil
}

type inlineSpanReader struct {
	size   int
	cursor int
	data   []byte
}

func (r *inlineSpanReader) Read(p []byte) (int, error) {
	if r.cursor >= r.size {
		return 0, io.EOF
	}
	if r.cursor >= len(r.data) {
		read := 0
		for r.cursor < r.size && read < len(p) {
			p[read] = 0
			read++
			r.cursor++
		}
		return read, nil
	}
	read := copy(p, r.data[r.cursor:])
	r.cursor += read
	return read, nil
}

func (r *inlineSpanReader) Close() error {
	return nil
}

type ReadSpanBufPool struct {
	pool sync.Pool
}

func NewReadSpanBufPool() *ReadSpanBufPool {
	pool := ReadSpanBufPool{
		pool: sync.Pool{
			New: func() any {
				buf := []byte{}
				return &buf
			},
		},
	}
	return &pool
}

// This does _not_ zero the memory in the bufs -- i.e. there might
// be garbage in it.
func (pool *ReadSpanBufPool) get(l int) *[]byte {
	buf := pool.pool.Get().(*[]byte)
	if cap(*buf) >= l {
		*buf = (*buf)[:l]
	} else {
		*buf = (*buf)[:cap(*buf)]
		*buf = append(*buf, make([]byte, l-len(*buf))...)
	}
	return buf
}

func (pool *ReadSpanBufPool) put(buf *[]byte) {
	if buf != nil {
		pool.pool.Put(buf)
	}
}

func (c *Client) ReadSpan(
	log *Logger,
	bufPool *ReadSpanBufPool,
	blacklist []msgs.BlockServiceId,
	id msgs.InodeId,
	blockServices []msgs.BlockService,
	fetchedSpan *msgs.FetchedSpan,
) (io.ReadCloser, error) {
	if fetchedSpan.Header.StorageClass == msgs.INLINE_STORAGE {
		data := fetchedSpan.Body.(*msgs.FetchedInlineSpan).Body
		dataCrc := msgs.Crc(crc32c.Sum(0, data))
		if dataCrc != fetchedSpan.Header.Crc {
			panic(fmt.Errorf("header CRC for inline span is %v, but data is %v", fetchedSpan.Header.Crc, dataCrc))
		}
		isr := inlineSpanReader{
			size:   int(fetchedSpan.Header.Size),
			cursor: 0,
			data:   fetchedSpan.Body.(*msgs.FetchedInlineSpan).Body,
		}
		return &isr, nil
	}

	body := fetchedSpan.Body.(*msgs.FetchedBlocksSpan)
	blocksCrcs := make([]msgs.Crc, body.Parity.Blocks())
	for i := range blocksCrcs {
		blocksCrcs[i] = body.Blocks[i].Crc
	}
	blockConn := func(blockIx int, offset uint32, size uint32) (io.ReadCloser, error) {
		block := body.Blocks[blockIx]
		blockService := blockServices[block.BlockServiceIx]
		for _, blacklisted := range blacklist {
			if blockService.Id == blacklisted {
				return nil, nil
			}
		}
		conn, err := c.GetBlocksConn(log, blockService.Id, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2)
		if err != nil {
			return nil, err
		}
		if err := FetchBlock(log, conn, &blockService, block.BlockId, block.Crc, offset, size); err != nil {
			conn.Close()
			return nil, err
		}
		return conn, err
	}
	return readSpanFromBlocks(bufPool, fetchedSpan.Header.Size, fetchedSpan.Header.Crc, body.Parity, body.Stripes, body.CellSize, blocksCrcs, body.StripesCrc, blockConn)
}
