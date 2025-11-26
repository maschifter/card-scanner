# MDB Server

This directory contains a simple HTTP file server built with Node.js and Express.js. Its purpose is to serve the generated ObjectBox `.mdb` database files over HTTP, allowing the mobile application to fetch them.

## Purpose

The `MDB_SERVER` provides a convenient way to make the `data.mdb` files, created by the `MDB_CREATOR` utility, accessible to other applications that need to download these database files.

## Setup

1.  **Install Node.js dependencies**:
    Navigate to the `DEV_UTILS/MDB_SERVER` directory and install the required packages:
    ```bash
    cd DEV_UTILS/MDB_SERVER
    npm install
    ```

## Usage

1.  **Place your `.mdb` files**:
    Ensure your generated `data.mdb` file (from the `MDB_CREATOR`) is located within the `public/*game_name*/` directory.

    Example: `DEV_UTILS/MDB_SERVER/public/lorcana/data.mdb`

2.  **Start the server**:
    From the `DEV_UTILS/MDB_SERVER` directory, run:

    ```bash
    node server.js
    ```

    The server will start on `http://localhost:3000` (or the port specified in `server.js`).

3.  **Access files**:
    You can access the `data.mdb` file in your mobile app (or any web browser) at:
    `http://localhost:3000/lorcana/data.mdb`
