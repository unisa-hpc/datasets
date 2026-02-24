# Datasets: MatrixMarket to CSR

This repository is used to convert Matrix Market (`.mtx`) graphs into CSR-based binary files.

It also contains a collection of graph datasets gathered from different public sources, including:
- [Network Repository](https://networkrepository.com/index.php)
- [SuiteSparse Matrix Collection](https://sparse.tamu.edu)

## Repository Structure

- `manager.py`: dataset management CLI (list, download, clean, convert)
- `_utils/`: Python utilities and C++ converter source (`converter.cpp`)
- `_converter/`: built converter binary output
- `library/`: CSR/MatrixMarket parsing and binary I/O headers
- `<dataset-name>/`: dataset folders with `info.yaml`, `.mtx`, and `.bin`

## Conversion

Convert one dataset:

```bash
python manager.py convert -y <dataset-name>
```

Force rebuild of the converter before conversion:

```bash
python manager.py convert --update -y <dataset-name>
```

Convert all datasets:

```bash
python manager.py convert -a -y
```

The converter reads Matrix Market files and writes CSR binary files compatible with the current library format.
