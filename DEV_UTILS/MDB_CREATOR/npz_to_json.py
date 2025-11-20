import argparse
import json
import os
import time

import numpy as np


def convert_npz_to_json(npz_path: str, json_path: str):
    """
    Ładuje dane z pliku NPZ i konwertuje je do pliku JSON.
    """
    if not os.path.exists(npz_path):
        print(f"Error: NPZ file was not found in: '{npz_path}'")
        return

    print(f"Loading data from '{npz_path}'...")
    try:
        data = np.load(npz_path)
        card_ids = data["card_ids"]
        texts = data["card_names"]
        embeddings = data["embeddings"]
        data.close()

        current_timestamp = int(time.time())
        date_created_timestamps = np.full(
            card_ids.shape[0], current_timestamp, dtype=np.int64
        )
    except KeyError as e:
        print(f"Error: Expected key not found in NPZ: {e}.")
        return
    except Exception as e:
        print(f"Error loadin NPZ file: {e}")
        return

    if not (
        card_ids.shape[0]
        == texts.shape[0]
        == date_created_timestamps.shape[0]
        == embeddings.shape[0]
    ):
        print("Error: Mismatch in element shapes inside NPZ file.")
        return

    print(f"Loaded {embeddings.shape[0]} cards. " f"Starting conversion into JSON...")

    all_cards_data = []

    for i in range(embeddings.shape[0]):
        card_id_str = (
            card_ids[i].decode("utf-8")
            if isinstance(card_ids[i], bytes)
            else str(card_ids[i])
        )
        text_str = (
            texts[i].decode("utf-8") if isinstance(texts[i], bytes) else str(texts[i])
        )

        card_data = {
            "card_id": card_id_str,
            "text": text_str,
            "date_created": int(date_created_timestamps[i]),
            "embedding": embeddings[i].astype(np.float32).tolist(),
        }
        all_cards_data.append(card_data)

    print(f"Saving {len(all_cards_data)} cards to '{json_path}'...")
    try:
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(all_cards_data, f, indent=4)
        print(f"Sucessfuly saved data into '{json_path}'.")
    except Exception as e:
        print(f"Error while saving JSON data {e}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert card data from  NPZ file to JSON."
    )
    parser.add_argument(
        "--npz_path",
        type=str,
        required=True,
        help="Path to input .npz file.",
    )
    parser.add_argument(
        "--json_out",
        type=str,
        required=True,
        help="Path to output .json file.",
    )
    args = parser.parse_args()

    convert_npz_to_json(args.npz_path, args.json_out)
