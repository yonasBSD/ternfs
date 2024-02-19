package cleanup

import (
	"testing"
	"time"
	"xtx/eggsfs/assert"
	"xtx/eggsfs/msgs"
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
	assert.Equal(t, 3, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(24 * time.Hour * 2)}, date(8), edges))
	// this falls on day 4, so between the second create and the deleted. the
	// delete is deleted too (can't be kept hanging)
	assert.Equal(t, 3, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(24 * time.Hour * 4)}, date(8), edges))
	// this falls between the first and second create.
	assert.Equal(t, 1, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(24 * time.Hour * 6)}, date(8), edges))
	// this is well before anything
	assert.Equal(t, 0, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(48 * time.Hour * 10)}, date(8), edges))
	// doesn't crash with empty edges
	assert.Equal(t, 0, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(48 * time.Hour * 10)}, date(8), make([]msgs.Edge, 0)))
}

func TestDontDeleteCurrent(t *testing.T) {
	edges := []msgs.Edge{
		{
			TargetId:     inodeId(1),
			NameHash:     0,
			Name:         "f",
			CreationTime: date(1),
			Current:      true,
		},
	}
	assert.Equal(t, 0, edgesToRemove(&msgs.SnapshotPolicy{DeleteAfterTime: msgs.ActiveDeleteAfterTime(time.Hour)}, date(5), edges))
}
