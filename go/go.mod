module xtx/eggsfs

go 1.18

require xtx/ecninfra v0.0.0-00010101000000-000000000000

require (
	github.com/hanwen/go-fuse/v2 v2.2.0
	golang.org/x/exp v0.0.0-20221126150942-6ab00d035af9
	golang.org/x/sys v0.4.0
)

require (
	github.com/pkg/errors v0.9.1 // indirect
	github.com/sirupsen/logrus v1.7.0 // indirect
)

replace xtx/ecninfra => ../../ecn/infra/go
