package utils

func Max[V uint8 | uint16 | uint32 | uint64 | int](x, y V) V {
	if x > y {
		return x
	} else {
		return y
	}
}

func Min[V uint8 | uint16 | uint32 | uint64 | int](x, y V) V {
	if x > y {
		return y
	} else {
		return x
	}
}
