import os
import sys
from unittest.mock import MagicMock

# Add current working directory to sys.path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Mock module imports before loading Flask_Web to avoid real DB and Redis connection
import Flask_Web
from Flask_Web import db, redis_client
from Flask_Web.models import Config, nhatky

# Setup mocks for SQLAlchemy and Redis
db.session = MagicMock()
redis_client.setex = MagicMock()
redis_client.lpush = MagicMock()
redis_client.ltrim = MagicMock()
redis_client.expire = MagicMock()

from Flask_Web.security import verify_telemetry_safety

class MockStation:
    id = 1
    name = "Test Station"
    username = "test_user"

class MockConfig:
    max_temp_charger = 60.0
    max_temp_battery = 50.0
    max_temp_env = 50.0
    max_humidity = 80.0
    limit_input_w = 1000.0

# Mock the query descriptor on Config class
mock_query = MagicMock()
mock_query.filter_by.return_value.first.return_value = MockConfig()
Config.query = mock_query

def test_safe_payload():
    station = MockStation()
    payload = {
        'state': 'DANG_SAC',
        'ds': 40.0,
        'mlx': 35.0,
        'dht_t': 30.0,
        'dht_h': 60.0,
        'p': 500.0,
        'i': 2.3,
        'v': 220.0,
        'sensor_ds_ok': True,
        'sensor_mlx_ok': True,
        'sensor_pzem_ok': True
    }
    is_safe, err = verify_telemetry_safety(station, payload)
    assert is_safe is True, f"Expected safe but got error: {err}"
    print("Safe payload test passed.")

def test_unsafe_payload_temperature():
    station = MockStation()
    payload = {
        'state': 'DANG_SAC',
        'ds': 65.0, # exceeds 60.0 max_temp_charger
        'mlx': 35.0,
        'dht_t': 30.0,
        'dht_h': 60.0,
        'p': 500.0,
        'i': 2.3,
        'v': 220.0,
    }
    is_safe, err = verify_telemetry_safety(station, payload)
    assert is_safe is False
    assert "Nhiệt độ bộ sạc" in err
    print("Unsafe temp test passed.")

def test_unsafe_absolute_voltage():
    station = MockStation()
    payload = {
        'state': 'SAN_SANG', # Idle, but has absolute check
        'ds': 40.0,
        'mlx': 35.0,
        'dht_t': 30.0,
        'dht_h': 60.0,
        'p': 0.0,
        'i': 0.0,
        'v': 270.0, # exceeds 265V absolute limit
    }
    is_safe, err = verify_telemetry_safety(station, payload)
    assert is_safe is False
    assert "Điện áp lưới quá cao" in err
    print("Absolute voltage check test passed.")

def test_na_payload_ignored():
    station = MockStation()
    payload = {
        'state': 'DANG_SAC',
        'ds': 'NA', # Should be ignored, so no error for ds
        'mlx': 35.0,
        'dht_t': 'N/A', # Should be ignored, so no error for dht_t
        'dht_h': 60.0,
        'p': 500.0,
        'i': 2.3,
        'v': 220.0,
        'sensor_ds_ok': True,
        'sensor_mlx_ok': True,
        'sensor_pzem_ok': True
    }
    is_safe, err = verify_telemetry_safety(station, payload)
    assert is_safe is True, f"Expected safe with NA values ignored, but got: {err}"
    print("NA values ignored test passed.")

if __name__ == '__main__':
    test_safe_payload()
    test_unsafe_payload_temperature()
    test_unsafe_absolute_voltage()
    test_na_payload_ignored()
    print("All security check unit tests passed successfully!")
