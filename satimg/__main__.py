"""Engine entry point for the desktop app:  python -m satimg --port 8765"""
import sys

import uvicorn

from satimg.app import app


def main():
    port = int(sys.argv[sys.argv.index("--port") + 1]) if "--port" in sys.argv else 8765
    uvicorn.run(app, host="127.0.0.1", port=port, log_level="warning")


if __name__ == "__main__":
    main()