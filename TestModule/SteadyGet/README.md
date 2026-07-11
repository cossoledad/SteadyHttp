# SteadyGet

A small wget-like program and independent consumer of the published
`steady-http/0.1.0` Conan package.

```bash
cd TestModule/SteadyGet
invoke configure --build-type=Debug --remote=radxa-conan-pr
invoke build --build-type=Debug

invoke download \
  --url=https://raw.githubusercontent.com/git/git/master/README.md \
  --output=git-readme.md
```

Direct executable usage:

```bash
./build/Debug/steady-get URL -o FILE \
  --max-size-mib 1024 --timeout-seconds 300
```

The output is first written to `FILE.part` and renamed only after a successful
download. Redirects and normal HTTPS certificate validation are handled by
SteadyHttp. This is intentionally not a full wget replacement: SteadyHttp 0.1
buffers the complete response in memory and has no live byte-progress callback.
