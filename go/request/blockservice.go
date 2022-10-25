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
	sock, err := net.DialTCP("tcp4", nil, &net.TCPAddr{IP: block.Ip[:], Port: int(block.Port)})
	if err != nil {
		return proof, fmt.Errorf("could not connect to block service %v:%d: %w", net.IP(block.Ip[:]), block.Port, err)
	}
	// message: ('e', block_id, certificate)
	var req [1 + 8 + 8]byte
	req[0] = 'e'
	binary.LittleEndian.PutUint64(req[1:], block.BlockId)
	copy(req[(1+8):], block.Certificate[:])
	logger.Debug("erasing block id %+v (msg %v)", block, req)
	_, err = sock.Write(req[:])
	if err != nil {
		return proof, fmt.Errorf("could not send message to block service %v:%d: %w", net.IP(block.Ip[:]), block.Port, err)
	}
	// response: ('E', block_id, proof)
	var resp [17]byte
	_, err = io.ReadFull(sock, resp[:])
	if err != nil {
		return proof, fmt.Errorf("could not receive response from block service %v:%d: %w", net.IP(block.Ip[:]), block.Port, err)
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
