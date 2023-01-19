package eggs

import "fmt"

func StorageClass(s string) uint8 {
	if s == "HDD" {
		return 2
	}
	if s == "FLASH" {
		return 3
	}
	panic(fmt.Errorf("bad storage class %v", s))
}
