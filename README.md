# Datasets: MatrixMarket to CSR

This repository is used to convert Matrix Market (`.mtx`) graphs into CSR-based binary files.

It also contains a collection of graph datasets gathered from different public sources, including:
- [Network Repository](https://networkrepository.com/index.php)
- [SuiteSparse Matrix Collection](https://sparse.tamu.edu)

## Repository Structure

- `manager.py`: dataset management CLI (list, download, clean, convert, transform)
- `_utils/`: Python utilities and C++ tools (`converter.cpp`, `transformer.cpp`)
- `_converter/`: built converter/transformer executable outputs
- `library/`: CSR/MatrixMarket parsing and binary I/O headers
- `<dataset-name>/`: dataset folders with `info.yaml`, `.mtx`, and `.bin`

## Transformation

Transform one dataset (`.mtx` -> `.transformed.mtx`):

```bash
python manager.py transform -y <dataset-name>
```

Typo-compatible alias:

```bash
python manager.py trasform -y <dataset-name>
```

Force rebuild of the transformer before processing:

```bash
python manager.py transform --update -y <dataset-name>
```

Transform all datasets:

```bash
python manager.py transform -a -y
```

Choose one or more transformations (ordered pipeline):

```bash
python manager.py transform --transformations sort -y <dataset-name>
python manager.py transform --transformations symmetrize -y <dataset-name>
python manager.py transform --transformations symmetrize sort -y <dataset-name>
python manager.py transform --transformations symmetrize sort binary -y <dataset-name>
```

The transformer writes `<basename>.transformed.mtx`.
- `sort`: only sorts edges by `(row, col)` ids.
- `symmetrize`: only symmetrizes to undirected form and deduplicates edge pairs.
- `binary`: converts the transformed Matrix Market file to `<basename>.transformed.bin`.
- Default pipeline: `symmetrize sort`.

When `binary` is requested, the transformed `.mtx` is kept by default. To discard it after a successful conversion:

```bash
python manager.py transform --transformations symmetrize sort binary --discard-transformed -y <dataset-name>
```

The binary stage uses the converter executable and can be customized with `--converter-path`.

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
