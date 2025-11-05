
CREATE TABLE IF NOT EXISTS kv_store (
    key_text TEXT PRIMARY KEY,
    value_text TEXT
);

CREATE SUBSCRIPTION sub_db1
CONNECTION 'host=db1 port=5432 user=stressuser password=stresspass dbname=stressdb'
PUBLICATION pub_db1
WITH (create_slot = true, enabled = true);

CREATE SUBSCRIPTION sub_db2
CONNECTION 'host=db2 port=5432 user=stressuser password=stresspass dbname=stressdb'
PUBLICATION pub_db2
WITH (create_slot = true, enabled = true);
