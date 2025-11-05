CREATE TABLE IF NOT EXISTS kv_store (
    key_text TEXT PRIMARY KEY,
    value_text TEXT
);

CREATE PUBLICATION pub_db2 FOR TABLE kv_store;
