package gc

import (
	"testing"
	"time"
	"xtx/eggsfs/msgs"

	"github.com/stretchr/testify/assert"
)

func inodeId(id uint64) msgs.OwnedInodeId {
	return msgs.MakeOwnedInodeId(msgs.MakeInodeId(msgs.FILE, 0, id), false)
}

func date(day int) msgs.EggsTime {
	return msgs.MakeEggsTime(time.Date(2021, time.January, day, 0, 0, 0, 0, time.UTC))
}

func TestKeepWithin(t *testing.T) {
	edges := []msgs.EdgeWithOwnership{
		{
			TargetId:     inodeId(1),
			NameHash:     0, // unneeded
			Name:         []byte("f"),
			CreationTime: date(1),
		},
		{
			TargetId:     inodeId(2),
			NameHash:     0, // unneeded
			Name:         []byte("f"),
			CreationTime: date(3),
		},
		{
			TargetId:     msgs.OwnedInodeId(msgs.NULL_INODE_ID),
			NameHash:     0, // unneeded
			Name:         []byte("f"),
			CreationTime: date(5),
		},
		{
			TargetId:     inodeId(3),
			NameHash:     0, // unneeded
			Name:         []byte("f"),
			CreationTime: date(7),
		},
	}
	assert.Panics(t, func() {
		assert.Equal(t, 0, (&Policy{}).edgesToRemove(msgs.Now(), edges))
	})
	assert.Equal(t, 0, (&Policy{DeleteAfterVersions: 10}).edgesToRemove(msgs.Now(), edges))
	assert.Equal(t, 3, (&Policy{DeleteAfterVersions: 1}).edgesToRemove(msgs.Now(), edges))
	assert.Equal(t, 1, (&Policy{DeleteAfterVersions: 2}).edgesToRemove(msgs.Now(), edges)) // delete does not count
	// last one never gets deleted
	assert.Equal(t, 3, (&Policy{DeleteAfterTime: time.Hour}).edgesToRemove(date(8), edges))
	// this falls on day 6, so between the delete and the last create. everything
	// apart from the last create should be deleted.
	assert.Equal(t, 3, (&Policy{DeleteAfterTime: 48 * time.Hour}).edgesToRemove(date(8), edges))
	// this falls on day 4, so between the second create and the deleted. only
	// the first create should be removed
	assert.Equal(t, 1, (&Policy{DeleteAfterTime: 48 * time.Hour * 2}).edgesToRemove(date(8), edges))
	// this is well before anything
	assert.Equal(t, 0, (&Policy{DeleteAfterTime: 48 * time.Hour * 10}).edgesToRemove(date(8), edges))
	// doesn't crash with empty edges
	assert.Equal(t, 0, (&Policy{DeleteAfterTime: 48 * time.Hour * 10}).edgesToRemove(date(8), make([]msgs.EdgeWithOwnership, 0)))
}
