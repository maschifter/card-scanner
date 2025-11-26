# MDB Creator


## ❗ CRITICAL WARNING: Schema Mismatch

The ObjectBox model schema file, `objectbox-model.json`, defines the structure of your database. For the application to read the database correctly, the schema used to **generate** the database must be **identical** to the schema used in the **application**.

**You must ensure that these two files are the same:**

1.  `DEV_UTILS/MDB_CREATOR/CPP_Generation/objectbox-model.json` (used for generation)
2.  `packages/react-native-card-scanner/objectbox-model.json` (used in the app)

If they are not identical, you will encounter runtime errors or unpredictable behavior. Before generating a new database, always synchronize these files.

---

## Workflow

1.  **Convert `.npz` to `.json`**: The `npz_to_json.py` script converts card data from a NumPy `.npz` file into a `.json` file. The `.npz` file must contain arrays for `card_ids`, `card_names`, and `embeddings`.

2.  **C++ Database Generation**: The `CPP_Generation` directory contains a C++ project that loads the data from the generated `.json` file and creates the final `data.mdb` file. This is the primary method for generating the database.

    -   **`main.cpp`**: The main application logic for reading the JSON file and writing to the ObjectBox database.
    -   **`schema.fbs`**: The FlatBuffers schema that defines the structure of the `Card` entity. This is used by the C++ generator to understand the data structure.
    -   **`CMakeLists.txt`**: The build script for the C++ application, which fetches ObjectBox and nlohmann/json dependencies.

## How to Use

### Prerequisites

-   Python 3
-   NumPy
-   CMake
-   A C++ compiler

### Steps

1.  **Convert `.npz` to `.json`**:

    ```bash
    python3 npz_to_json.py --npz_path <path_to_your_data.npz> --json_out ./CPP_Generation/output.json
    ```
    *This will place the generated `output.json` inside the `CPP_Generation` directory.*

2.  **Build and Run the C++ Application**:

    Navigate to the `CPP_Generation` directory:

    ```bash
    cd CPP_Generation
    ```

    Create a build directory and run CMake:

    ```bash
    mkdir -p build && cd build
    cmake ..
    ```

    Build the project:

    ```bash
    cmake --build .
    ```

    Run the `json_loader` executable from the `build` directory to generate the database:

    ```bash
    ./json_loader ../output.json ../db_out
    ```

    This will create `data.mdb` and `lock.mdb` in the `CPP_Generation/db_out` directory. You can then copy the `data.mdb` file to your application's assets.
