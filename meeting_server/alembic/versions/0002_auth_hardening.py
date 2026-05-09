"""auth hardening for users and sessions

Revision ID: 0002_auth_hardening
Revises: 0001_init_meeting_schema
Create Date: 2026-05-08 18:35:00
"""

from alembic import op
import sqlalchemy as sa


revision = "0002_auth_hardening"
down_revision = "0001_init_meeting_schema"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column("users", sa.Column("password_hash", sa.String(length=256), nullable=True))
    op.add_column("sessions", sa.Column("revoked_at", sa.DateTime(), nullable=True))


def downgrade() -> None:
    op.drop_column("sessions", "revoked_at")
    op.drop_column("users", "password_hash")
