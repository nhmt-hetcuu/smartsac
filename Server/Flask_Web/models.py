from sqlalchemy import Column, Integer, String, Float, Boolean, DateTime, UniqueConstraint
from sqlalchemy.dialects.mysql import LONGTEXT
from Flask_Web import db ,app
from datetime import datetime
from flask_login import UserMixin

class BaseModel(db.Model):
    __abstract__ = True
    id = Column(Integer, primary_key=True, autoincrement=True)
    def __repr__(self):
        return f'<{self.__class__.__name__} id={self.id}>'

#tai khoan
class User(BaseModel,UserMixin):
    username=Column(String(256))
    password=Column(String(512))
    email=Column(String(512))
    api_zalo = Column(String(512))
    api_telegram = Column(String(512))
    id_client_telegram = Column(String(512))
    date_create=Column(DateTime,default=datetime.now)
    quyen=Column(String(10),default='user')
    date_mail = Column(String(512), default='2020-01-01 00:00:00.000000')

class nhatky(BaseModel):
    username=Column(String(256))
    noidung=Column(LONGTEXT)
    loai=Column(String(128))
    date = Column(DateTime, default=datetime.now)

class Station(BaseModel):
    username=Column(String(256))
    name=Column(String(256))
    token=Column(String(256), unique=True)
    status=Column(String(20), default='offline')
    is_active=Column(Boolean, default=False)
    date_create=Column(DateTime, default=datetime.now)
    date_update=Column(DateTime, default=datetime.now, onupdate=datetime.now)

class Config(BaseModel):
    username=Column(String(256))
    station_id=Column(Integer)
    battery_type=Column(String(50), default='lead')
    voltage=Column(Integer, default=48)
    capacity_ah=Column(Integer, default=20)
    output_v=Column(Integer, default=60)
    output_a=Column(Float, default=3.0)
    max_time_h=Column(Integer)
    threshold_w=Column(Float)
    measure_interval=Column(Integer, default=300)
    wait_time=Column(Integer, default=60)
    limit_input_w=Column(Integer, default=1000)
    max_temp_charger=Column(Integer)
    max_temp_battery=Column(Integer)
    max_temp_env=Column(Integer, default=50)
    max_humidity=Column(Integer, default=80)
    date_create=Column(DateTime, default=datetime.now)
    date_update=Column(DateTime, default=datetime.now, onupdate=datetime.now)


class MonthlyChargeStat(BaseModel):
    __table_args__ = (
        UniqueConstraint('station_id', 'year', 'month', name='uq_station_monthly_charge'),
    )

    station_id = Column(Integer, index=True, nullable=False)
    year = Column(Integer, nullable=False)
    month = Column(Integer, nullable=False)
    energy_kwh = Column(Float, default=0.0)
    power_sum_w = Column(Float, default=0.0)
    sample_count = Column(Integer, default=0)
    date_create = Column(DateTime, default=datetime.now)
    date_update = Column(DateTime, default=datetime.now, onupdate=datetime.now)


if __name__=='__main__':
    app.app_context().push()
    #db.create_all()
