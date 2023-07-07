# Changelog

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

## [v9.0.0]

### Added

- Add a new `.Set` that can be introspected to see if things are set.
  (thanks @razor-1)

### Fixed

- Fix []byte to be base64 encoded in json to match how []byte is handled
  (thanks @razor-1)

## [v8.1.2]

### Fixed

- Scan negative int64 values into uint64 without breaking

## [v8.1.1]

### Fixed

- Fix an overflow issue in uint64

## [v8.1.0]

### Changed

- Version the code with go modules

## [v8.0.0]

### Changed

- Update code for new randomizer interface
- Start versioning with semantic versions
- Add tags for older versions (back to 7.1.0)
