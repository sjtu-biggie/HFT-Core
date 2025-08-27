
prometheus --config.file="config/prometheus-grafana-cloud.yml" \
           --storage.tsdb.retention.time=6h \
           --storage.tsdb.retention.size=1GB \
           --storage.tsdb.wal-compression