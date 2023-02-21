package lib

import (
	"testing"
	"time"
	"xtx/eggsfs/msgs"

	"github.com/stretchr/testify/assert"
)

func inodeId(id uint64) msgs.InodeIdExtra {
	return msgs.MakeInodeIdExtra(msgs.MakeInodeId(msgs.FILE, 0, id), false)
}

func date(day int) msgs.EggsTime {
	return msgs.MakeEggsTime(time.Date(2021, time.January, day, 0, 0, 0, 0, time.UTC))
}

func TestDeleteAll(t *testing.T) {
	edges := []msgs.Edge{
		{
			TargetId:     inodeId(1),
			NameHash:     0, // unneeded
			Name:         "f",
			CreationTime: date(1),
		},
		{
			TargetId:     msgs.InodeIdExtra(msgs.NULL_INODE_ID),
			NameHash:     0, // uneeded
			Name:         "f",
			CreationTime: date(2),
		},
	}
	assert.Equal(t, 2, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterVersions: msgs.ActiveDeleteAfterVersions(0)}, date(3), edges))
	assert.Equal(t, 2, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(0)}, date(3), edges))
}

func TestKeepWithin(t *testing.T) {
	edges := []msgs.Edge{
		{
			TargetId:     inodeId(1),
			NameHash:     0, // unneeded
			Name:         "f",
			CreationTime: date(1),
		},
		{
			TargetId:     inodeId(2),
			NameHash:     0, // unneeded
			Name:         "f",
			CreationTime: date(3),
		},
		{
			TargetId:     msgs.InodeIdExtra(msgs.NULL_INODE_ID),
			NameHash:     0, // unneeded
			Name:         "f",
			CreationTime: date(5),
		},
		{
			TargetId:     inodeId(3),
			NameHash:     0, // unneeded
			Name:         "f",
			CreationTime: date(7),
		},
	}
	assert.Equal(t, 0, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterVersions: msgs.ActiveDeleteAfterVersions(10)}, msgs.Now(), edges))
	assert.Equal(t, 3, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterVersions: msgs.ActiveDeleteAfterVersions(1)}, msgs.Now(), edges))
	assert.Equal(t, 1, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterVersions: msgs.ActiveDeleteAfterVersions(2)}, msgs.Now(), edges)) // delete does not count
	// this falls on day 6, so between the delete and the last create. everything
	// apart from the last create should be deleted.
	assert.Equal(t, 3, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(48 * time.Hour)}, date(8), edges))
	// this falls on day 4, so between the second create and the deleted. only
	// the first create should be removed
	assert.Equal(t, 1, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(48 * time.Hour * 2)}, date(8), edges))
	// this is well before anything
	assert.Equal(t, 0, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(48 * time.Hour * 10)}, date(8), edges))
	// doesn't crash with empty edges
	assert.Equal(t, 0, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(48 * time.Hour * 10)}, date(8), make([]msgs.Edge, 0)))
}
