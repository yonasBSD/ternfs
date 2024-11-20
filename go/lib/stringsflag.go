package lib

import "fmt"

type StringArrayFlags []string


// String is an implementation of the flag.Value interface
func (i *StringArrayFlags) String() string {
    return fmt.Sprintf("%v", *i)
}

// Set is an implementation of the flag.Value interface
func (i *StringArrayFlags) Set(value string) error {
    *i = append(*i, value)
    return nil
}
