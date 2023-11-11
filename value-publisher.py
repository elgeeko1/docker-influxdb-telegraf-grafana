#!/usr/bin/env python

import time

# post to telegraf
import requests

# Define the URL for the HTTP listener of Telegraf
TELEGRAPH_URL = "http://telegraf:8186/telegraf"

print("Starting up.")
telegraf_session = requests.Session()
value = 0.0
count = 0
last_log_print_time = time.time()
try:
    while True:
        # Convert the timestamp to nanoseconds
        timestamp_ns = int(time.time() * 1e9)

        try:
            telegraf_session.post(
                TELEGRAPH_URL,
                data = f"value value={value} {timestamp_ns}",
                headers = {'Content-Type': 'application/x-www-form-urlencoded'})
            
            # increment value and sleep for a period
            value = (value + 0.1) % 10
            count = count + 1

            # print publish count every 5s
            if time.time() - last_log_print_time > 5:
                print("total published values: " + str(count))
                last_log_print_time = time.time()
        except requests.RequestException as e:
            print(e)
        finally:
            time.sleep(0.100)
except KeyboardInterrupt:
    print("Shutting down.")
finally:
    telegraf_session.close()