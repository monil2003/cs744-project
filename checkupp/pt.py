import psycopg2
import time

conn_str = "postgresql://neondb_owner:npg_brP9kR7mxGWs@ep-fancy-glade-adxpjfbo-pooler.c-2.us-east-1.aws.neon.tech/neondb?sslmode=require&channel_binding=require"
t=0
for i in range(10):
    start = time.time()
    conn = psycopg2.connect(conn_str)
    end = time.time()
    t+=(end - start)*1000
    conn.close()
t=t/10
print("Avg Response rate in ms:",t)
