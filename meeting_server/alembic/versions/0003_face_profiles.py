"""add face profile storage

Revision ID: 0003_face_profiles
Revises: 0002_auth_hardening
Create Date: 2026-05-27 12:00:00
"""

from alembic import op
import sqlalchemy as sa


revision = "0003_face_profiles"
down_revision = "0002_auth_hardening"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "face_profiles",
        sa.Column("id", sa.Integer(), nullable=False),
        sa.Column("user_id", sa.Integer(), nullable=False),
        sa.Column("image_base64", sa.Text(), nullable=False),
        sa.Column("created_at", sa.DateTime(), nullable=False),
        sa.Column("updated_at", sa.DateTime(), nullable=False),
        sa.ForeignKeyConstraint(["user_id"], ["users.id"]),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("user_id"),
    )
    op.create_index(op.f("ix_face_profiles_user_id"), "face_profiles", ["user_id"], unique=False)


def downgrade() -> None:
    op.drop_index(op.f("ix_face_profiles_user_id"), table_name="face_profiles")
    op.drop_table("face_profiles")
