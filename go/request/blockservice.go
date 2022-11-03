package request

import (
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"xtx/eggsfs/loglevels"
	"xtx/eggsfs/msgs"
)

// TODO connection pool rather than opening a new one each time
func EraseBlock(
	logger loglevels.LogLevels,
	block msgs.BlockInfo,
) ([8]byte, error) {
	var proof [8]byte
	sock, err := net.DialTCP("tcp4", nil, &net.TCPAddr{IP: block.BlockServiceIp[:], Port: int(block.BlockServicePort)})
	if err != nil {
		return proof, fmt.Errorf("could not connect to block service %v:%d: %w", net.IP(block.BlockServiceIp[:]), block.BlockServicePort, err)
	}
	// message: (block_service_id, 'e', block_id, certificate)
	var req [8 + 1 + 8 + 8]byte
	binary.LittleEndian.PutUint64(req[:], uint64(block.BlockServiceId))
	req[8] = 'e'
	binary.LittleEndian.PutUint64(req[(8+1):], block.BlockId)
	copy(req[(8+1+8):], block.Certificate[:])
	logger.Debug("erasing block id %+v (msg %v)", block, req)
	_, err = sock.Write(req[:])
	if err != nil {
		return proof, fmt.Errorf("could not send message to block service %v:%d: %w", net.IP(block.BlockServiceIp[:]), block.BlockServicePort, err)
	}
	// response: ('E', block_id, proof)
	var resp [17]byte
	_, err = io.ReadFull(sock, resp[:])
	if err != nil {
		return proof, fmt.Errorf("could not receive response from block service %v:%d: %w", net.IP(block.BlockServiceIp[:]), block.BlockServicePort, err)
	}
	if resp[0] != 'E' {
		return proof, fmt.Errorf("bad response kind from block service: %v rather than %v", resp[0], 'E')
	}
	respBlockid := binary.LittleEndian.Uint64(resp[1:])
	if respBlockid != block.BlockId {
		return proof, fmt.Errorf("bad response block id from block service: %v rather than %v", respBlockid, block.BlockId)
	}
	copy(proof[:], resp[(1+8):])
	return proof, nil
}
