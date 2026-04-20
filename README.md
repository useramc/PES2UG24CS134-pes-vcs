# PES Version Control System (PES-VCS)

## Overview
This project is a simplified version control system inspired by Git. It implements core filesystem concepts such as content-addressable storage, staging areas, atomic writes, and commit history tracking.

The system allows users to initialize a repository, stage files, create commits, and view history.

---

## Features Implemented

### Phase 1: Object Storage
- Implemented blob storage using content-addressable hashing  
- Stored file contents in `.pes/objects/`  
- Each object is identified using a SHA-based hash  

---

### Phase 2: Trees (Directory Structure)
- Built tree objects to represent directory hierarchy  
- Linked blobs and subtrees recursively  
- Stored structured snapshots of directories  

---

### Phase 3: Index (Staging Area)
- Implemented `.pes/index` as a text-based staging file  
- Tracked:
  - file mode  
  - content hash  
  - modification time  
  - file size  
  - file path  

#### Key Functions:
- `index_load` → loads index from disk  
- `index_save` → atomic write using temp file + rename  
- `index_add` → stages files and updates index  

#### Functionality:
- Detects staged, modified, deleted, and untracked files  
- Uses metadata (mtime, size) for fast change detection  

---

### Phase 4: Commits and History
- Implemented commit creation and linking  

#### Key Function:
- `commit_create`

#### Workflow:
1. Build tree from staged index  
2. Read parent commit from HEAD  
3. Create commit object with:
   - tree reference  
   - parent reference  
   - author  
   - timestamp  
   - message  
4. Store commit in object database  
5. Update HEAD reference  

#### Output:
- `pes log` shows full commit history  
- Commits form a linked chain  

---

## File Structure

.pes/
│── objects/          # Blob, tree, commit storage
│── refs/heads/main   # Current branch pointer
│── HEAD              # Points to active branch
│── index             # Staging area

---

## Commands Supported

| Command | Description |
|--------|------------|
| ./pes init | Initialize repository |
| ./pes add <file> | Stage file |
| ./pes status | Show staged/unstaged changes |
| ./pes commit -m "msg" | Create commit |
| ./pes log | View commit history |

---

## Screenshots

Screenshots are included in the `screenshots/` folder as well as can be found in REPORT.pdf

---

## Key Concepts Learned

- Content-addressable storage  
- Atomic file operations (fsync + rename)  
- Metadata-based change detection  
- Linked commit structures  
- Reference-based version tracking  

---

## Conclusion
This project demonstrates how a version control system internally manages files, tracks changes, and maintains history using filesystem-level abstractions.

It provides a strong foundation for understanding how systems like Git operate under the hood.
