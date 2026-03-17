LLVM Pass for GraphViz control-data flow
----------------------------------------

### Build
```sh
clang++ graph_pass.cpp -fPIC -shared -I$(llvm-config --includedir) -o graphPass.so
````

### Run

Compiling with this pass will emit `dot` syntax for the program's flow

```sh
clang -fpass-plugin=./graphPass.so <source_file>
```

### Examples

<table>
  <tr>
    <td align="center" valign="middle">
      <img src="assets/hello_O0.png" alt="Graph for 'hello.c'" width="420">
    </td>
    <td align="center" valign="middle">
      <img src="assets/fact_O1.png" alt="Graph for 'fact.c'" width="420">
    </td>
  </tr>
  <tr>
    <td align="center" valign="top">
      <code>clang -fpass-plugin=./graphPass.so examples/hello.c -O0 | dot</code>
    </td>
    <td align="center" valign="top">
      <code>clang -fpass-plugin=./graphPass.so examples/fact.c -O1 | dot</code>
    </td>
  </tr>
</table>
