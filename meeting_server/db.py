from sqlalchemy import create_engine
from sqlalchemy.orm import DeclarativeBase, sessionmaker

from config import load_config


class Base(DeclarativeBase):
    pass


CONFIG = load_config()
_connect_args = {"check_same_thread": False} if CONFIG.database_url.startswith("sqlite") else {}
engine = create_engine(CONFIG.database_url, future=True, connect_args=_connect_args)
SessionLocal = sessionmaker(bind=engine, autoflush=False, autocommit=False, future=True)
