package eggs

import (
	"encoding/json"
	"fmt"
	"net/http"
	"xtx/eggsfs/msgs"
)

type BlockService struct {
	Id            msgs.BlockServiceId `json:"id"`
	Ip            string              `json:"ip"`
	SecretKey     string              `json:"secret_key"`
	Port          uint16              `json:"port"`
	StorageClass  uint8               `json:"storage_class"`
	FailureDomain string              `json:"failure_domain"`
}

func GetAllBlockServices(shuckleHost string) ([]BlockService, error) {
	resp, err := http.Get(fmt.Sprintf("http://%s/all_block_services", shuckleHost))
	if err != nil {
		return nil, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	var bss []BlockService
	if err := json.NewDecoder(resp.Body).Decode(&bss); err != nil {
		return nil, fmt.Errorf("json decoding failed: %w", err)
	}

	return bss, nil
}
