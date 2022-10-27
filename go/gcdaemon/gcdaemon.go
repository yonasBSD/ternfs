package main

import (
	"flag"
	"xtx/eggsfs/gc"
)

func main() {
	verbose := flag.Bool("verbose", false, "enables debug logging")
	flag.Parse()
	gc.Run(*verbose)
}
