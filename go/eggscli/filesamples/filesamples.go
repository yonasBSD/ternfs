package filesamples

import (
	"encoding/json"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"slices"
	"strings"
	"sync"
	"xtx/eggsfs/client"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

type FileSample struct {
	Owner        msgs.InodeId  `json:"owner"`
	Inode        msgs.InodeId  `json:"inode"`
	Name         string        `json:"name"`
	Path         string        `json:"path"`
	Current      bool          `json:"current"`
	LogicalSize  uint64        `json:"logical"`
	HDDSize      uint64        `json:"hdd"`
	FlashSize    uint64        `json:"flash"`
	InlineSize   uint64        `json:"inline"`
	CreationTime msgs.EggsTime `json:"creation_time"`
	DeletionTime msgs.EggsTime `json:"deletion_time"`
	MTime        msgs.EggsTime `json:"mtime"`
	ATime        msgs.EggsTime `json:"atime"`
	SizeWeight   uint64        `json:"size_weight"`
}

type PathResolver interface {
	// Given an inode ID and the name of a file, returns the full path to the file.
	Resolve(inode msgs.InodeId, filename string) (string, error)

	// Given a directory containing `sample-files` outputs, resolves the full path for every sample
	// in every file in the directory.
	// For example, given a directory such as:
	//   /path/to/samples/
	//     - shard_000_0.json
	//     - shard_001_0.json
	//     - shard_002_0.json
	// This will load each of the json files, determine the full path for each sample, and then
	// write ALL of the enriched samples into the output file.
	ResolveFilePaths(sampleFilesDir string, outputFileName string) error
}

// Returns a thread-safe PathResolver that can be used concurrently in multiple goroutines.
func NewPathResolver(cl *client.Client, logger *lib.Logger) PathResolver {
	return &resolver{
		eggsClient: cl,
		logger:     logger,
		inodeToDir: make(map[msgs.InodeId]string),
		lock:       sync.RWMutex{},
	}
}

type resolver struct {
	eggsClient *client.Client
	logger     *lib.Logger
	// Mapping of inode ID to directory name. Used to avoid duplicate lookups for the same inode.
	inodeToDir map[msgs.InodeId]string
	// Used to handle concurrent access to resolver internal data.
	lock sync.RWMutex
}

func (r *resolver) Resolve(ownerInode msgs.InodeId, filename string) (string, error) {
	filepath := filename
	currentDir := ownerInode
	for {
		// Get the next owner inode.
		statReq := msgs.StatDirectoryReq{
			Id: currentDir,
		}
		statResp := msgs.StatDirectoryResp{}
		if err := r.eggsClient.ShardRequest(r.logger, currentDir.Shard(), &statReq, &statResp); err != nil {
			return "", fmt.Errorf("StatDirectoryReq to shard %v for inode %v failed: %w", currentDir.Shard(), currentDir, err)
		}
		owner := statResp.Owner
		// If there is nothing else to chase, then we're done.
		if owner == msgs.NULL_INODE_ID {
			return filepath, nil
		}
		if owner == msgs.ROOT_DIR_INODE_ID {
			return path.Join("/", filepath), nil
		}
		// Get the name of the next node locally if possible.
		if dirName, exists := r.getDirName(currentDir); exists {
			filepath = path.Join(dirName, filepath)
			currentDir = owner
			continue
		}
		dirName, err := r.getNameFromShard(owner, currentDir)
		if err != nil {
			return "", fmt.Errorf("failed to lookup directory name from shard: %w", err)
		}
		r.setDirName(currentDir, dirName)
		filepath = path.Join(dirName, filepath)
		currentDir = owner
	}
}

func (r *resolver) ResolveFilePaths(sampleFilesDir string, outputFileName string) error {
	// We open the output file first to make sure that it's accessible _before_ we do anything expensive.
	outputFile, err := os.Create(outputFileName)
	if err != nil {
		return fmt.Errorf("failed to open output file: %w", err)
	}
	// Load all of the sample files.
	r.logger.Info("Loading sample files")
	sampleFiles, err := filepath.Glob(path.Join(sampleFilesDir, "*.json"))
	if err != nil {
		return fmt.Errorf("failed to identify sample files: %w", err)
	}
	// Remove the output file if it happens to have been included in the sample files list.
	sampleFiles = slices.DeleteFunc(sampleFiles, func(sampleFile string) bool {
		// If either path ends with the other, then they're probably pointing at the same location.
		return strings.HasSuffix(sampleFile, outputFileName) || strings.HasSuffix(outputFileName, sampleFile)
	})
	samples := make([][]*FileSample, len(sampleFiles))
	for i := range samples {
		samples[i] = make([]*FileSample, 0)
	}
	errChan := make(chan error, len(sampleFiles))
	wg := sync.WaitGroup{}
	for i, sampleFile := range sampleFiles {
		wg.Add(1)
		go func() {
			defer wg.Done()

			f, err := os.Open(sampleFile)
			if err != nil {
				errChan <- fmt.Errorf("failed to open sample file %v: %w", sampleFile, err)
			}
			decoder := json.NewDecoder(f)
			err = decoder.Decode(&samples[i])
			if err != nil {
				errChan <- fmt.Errorf("failed to decode sample file %v: %w", sampleFile, err)
			}
		}()
	}
	r.logger.Info("Waiting for sample files to load")
	wg.Wait()
	loadingErrors := false
errorReportingLoop:
	for {
		select {
		case err := <-errChan:
			loadingErrors = true
			r.logger.ErrorNoAlert("%v", err)
		default:
			break errorReportingLoop
		}
	}
	if loadingErrors {
		return fmt.Errorf("an error occurred while loading sample files, check the logs")
	}
	numSamples := 0
	for _, sampleSet := range samples {
		numSamples += len(sampleSet)
	}
	idx := 0
	outputSamples := make([]*FileSample, numSamples)
	for _, sampleSet := range samples {
		for _, sample := range sampleSet {
			outputSamples[idx] = sample
			idx += 1
		}
	}
	r.logger.Info("Sample files loaded - %d samples found", numSamples)
	// Resolve all of the sample files.
	r.logger.Info("Resolving file paths")
	type task struct {
		idx    int
		sample *FileSample
	}
	// Start the workers going.
	numWorkers := 100
	workQueue := make(chan *task, numWorkers)
	for range numWorkers {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				t, ok := <-workQueue
				if !ok {
					return
				}
				path, err := r.Resolve(t.sample.Owner, t.sample.Name)
				if err != nil {
					r.logger.ErrorNoAlert("Failed to resolve file path: %v", err)
				}
				if t.idx%1000 == 0 {
					r.logger.Info("%d samples processed", t.idx)
				}
				t.sample.Path = path
			}
		}()
	}

	for i, sample := range outputSamples {
		workQueue <- &task{idx: i, sample: sample}
	}
	close(workQueue)
	wg.Wait()
	r.logger.Info("Path resolution complete")

	// Write the output.
	r.logger.Info("Writing output file")
	encoder := json.NewEncoder(outputFile)
	err = encoder.Encode(outputSamples)
	if err != nil {
		return fmt.Errorf("failed to write samples to the output file: %w", err)
	}
	err = outputFile.Close()
	if err != nil {
		return fmt.Errorf("failed to close the output file: %w", err)
	}
	r.logger.Info("Output file written")
	return nil
}

// Queries the shards to list the contents of the parent directory and get the name of the target inode.
func (r *resolver) getNameFromShard(parentDir msgs.InodeId, target msgs.InodeId) (string, error) {
	readDirReq := msgs.FullReadDirReq{
		DirId: parentDir,
	}
	for {
		readDirResp := msgs.FullReadDirResp{}
		if err := r.eggsClient.ShardRequest(r.logger, parentDir.Shard(), &readDirReq, &readDirResp); err != nil {
			return "", fmt.Errorf("FullReadDirReq to shard failed: %w", err)
		}
		for _, result := range readDirResp.Results {
			if result.TargetId.Id() != target {
				continue
			}
			return result.Name, nil
		}
		// If the cursor doesn't have a start name, then we've seen everything.
		if readDirResp.Next.StartName == "" {
			break
		}

		readDirReq.StartTime = readDirResp.Next.StartTime
		readDirReq.StartName = readDirResp.Next.StartName
		if readDirResp.Next.Current {
			readDirReq.Flags = msgs.FULL_READ_DIR_CURRENT
		}
	}

	return "", fmt.Errorf("failed to find directory name")

}

func (r *resolver) getDirName(inode msgs.InodeId) (string, bool) {
	r.lock.RLock()
	defer r.lock.RUnlock()

	val, exists := r.inodeToDir[inode]
	return val, exists
}

func (r *resolver) setDirName(inode msgs.InodeId, name string) {
	r.lock.Lock()
	defer r.lock.Unlock()

	r.inodeToDir[inode] = name
}
