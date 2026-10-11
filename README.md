Renderer migration review evidence

Foundation tested source: 825e5ea55eb7a65a790ced33a419b18d8d8ffd62
Base: c6a7eae787a0f945619a622ed99ac755c2632689
Linux x86_64; Python 3.12.3. The committed tree was extracted independently with git archive.
Command: python3 -m unittest discover -s test/build_system -p test_renderer_shaders.py -v
Result: 16 passed. Offline generator tests only; no hardware or native compiler validation claimed.

Additional integrated runtime evidence will be published here with its own exact source revision and limitations.
