from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import Dict
import random
import os
import json

app = FastAPI()

# Simulated sensors and configuration store
sensors: Dict[str, float] = {}
config_dir = "configs"
os.makedirs(config_dir, exist_ok=True)


# Helper to simulate sensor readings
def simulate_sensor_value() -> float:
    return round(random.uniform(10.0, 100.0), 2)


# GET: Read sensor value
@app.get("/sensor/{sensor_id}")
def get_sensor_value(sensor_id: str):
    value = sensors.get(sensor_id, simulate_sensor_value())
    sensors[sensor_id] = value  # Persist simulated value

    config_path = os.path.join(config_dir, f"{sensor_id}.json")
    if os.path.exists(config_path):
        with open(config_path, "r") as f:
            config = json.load(f)
        try:
            scale_str = config.get("scale", "1x")
            unit = config.get("unit", "Unknown")
            scale = float(scale_str.replace("x", ""))
            value *= scale
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"Error applying configuration: {e}")
    else:
        return {
            "sensor_id": sensor_id,
            "raw_value": "Sensor is not configured",
        }

    return {
        "sensor_id": sensor_id,
        "raw_value": sensors[sensor_id],
        "scaled_value": round(value, 2),
        "scale": f"{scale}x",
        "unit": unit
    }


# POST: Create default config
@app.post("/sensor/{sensor_id}")
def create_config(sensor_id: str):
    config_path = os.path.join(config_dir, f"{sensor_id}.json")
    if os.path.exists(config_path):
        raise HTTPException(
            status_code=409,
            detail=f"Configuration for sensor '{sensor_id}' already exists."
        )
    default_config = {"scale": "1x", "unit": "Celsius"}
    with open(config_path, "w") as f:
        json.dump(default_config, f)
    return {"message": f"Configuration file for sensor '{sensor_id}' created."}


# PUT: Replace config file
class ConfigModel(BaseModel):
    scale: str
    unit: str


@app.put("/sensor/{sensor_id}")
def update_config(sensor_id: str, config: ConfigModel):
    config_path = os.path.join(config_dir, f"{sensor_id}.json")
    if not os.path.exists(config_path):
        raise HTTPException(
            status_code=406,
            detail=f"Configuration for sensor '{sensor_id}' does not exist and cannot be created via PUT."
        )
    with open(config_path, "w") as f:
        json.dump(config.dict(), f)
    return {"message": f"Configuration file for sensor '{sensor_id}' updated."}