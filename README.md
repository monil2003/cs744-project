# CS744 DECS  
## Project: HTTP-based Key-Value Server

This project implements a **distributed HTTP-based Key-Value (KV) store** with **PostgreSQL logical replication** for reliability and fault tolerance. The system uses multiple PostgreSQL instances managed through a lightweight, multi-threaded HTTP server.

---

### 🧩 Overview

- **HTTP Server (httplib, localhost:8080):**  
  Handles concurrent client requests using thread pools and exposes RESTful endpoints for KV operations.

- **PostgreSQL Databases:**  
  - `db1` (port 5432) and `db2` (port 5433) act as primary databases.  
  - `db3` (port 5434) acts as a replica subscribing to both primaries for high availability.

- **Replication:**  
  Logical replication ensures data consistency across nodes, with `db3` automatically synchronizing updates from both primaries.

- **Modes of Operation:**  
  - **Replicated Mode:** db1, db2, and db3 (high availability)  
  - **Direct Mode:** db1 and db2 only (performance testing)

---

### ⚙️ Features

- Multi-threaded HTTP request handling  
- LRU caching for frequent lookups  
- Manual PostgreSQL replication setup  
- Fault-tolerant data access via db3 fallback  
- Docker-based isolated deployment

---

### 🚀 Running the System

1. Start all PostgreSQL containers (`db1`, `db2`, `db3`).
2. First go to databases folder & start the docker.
   ```bash
   cd Databases
   docker compose up -d
3. Manually configure replication as per the documentation.
4. Next go to server folder
   ```bash
   cd ../server
6. Launch the HTTP server:  
   ```bash
   ./server_app
   Can also use cmake files in the folder to re-compile and then run the above code.
   ```bash
   cd build
   cmake --build .
7. Can accesss txt files in each folder for code snippets for running

Author: Monil Manish Desai
