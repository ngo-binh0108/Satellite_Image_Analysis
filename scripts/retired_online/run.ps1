# Starts the web app at http://127.0.0.1:8000
& "$PSScriptRoot\.conda-env\python.exe" -m uvicorn satimg.app:app --app-dir "$PSScriptRoot" --host 127.0.0.1 --port 8000
