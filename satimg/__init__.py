import os

# The numeric libraries start one thread per core by default. On machines with many cores that
# makes model prediction far slower, not faster (measured: 30 s with 32 threads, 1 s with 8).
# Must be set before numpy / scikit-learn are imported.
os.environ.setdefault("OMP_NUM_THREADS", str(min(8, os.cpu_count() or 1)))
os.environ.setdefault("OPENBLAS_NUM_THREADS", os.environ["OMP_NUM_THREADS"])