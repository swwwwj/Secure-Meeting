"""support multiple named face profiles per user

Revision ID: 0004_multi_face_profiles
Revises: 0003_face_profiles
Create Date: 2026-05-28 23:30:00
"""

from alembic import op
import sqlalchemy as sa


revision = "0004_multi_face_profiles"
down_revision = "0003_face_profiles"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "face_profile_entries",
        sa.Column("id", sa.Integer(), nullable=False),
        sa.Column("user_id", sa.Integer(), nullable=False),
        sa.Column("profile_name", sa.String(length=64), nullable=False),
        sa.Column("image_base64", sa.Text(), nullable=False),
        sa.Column("created_at", sa.DateTime(), nullable=False),
        sa.Column("updated_at", sa.DateTime(), nullable=False),
        sa.ForeignKeyConstraint(["user_id"], ["users.id"]),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("user_id", "profile_name", name="uq_face_profile_entries_user_name"),
    )
    op.create_index(op.f("ix_face_profile_entries_user_id"), "face_profile_entries", ["user_id"], unique=False)

    op.execute(
        """
        INSERT INTO face_profile_entries (user_id, profile_name, image_base64, created_at, updated_at)
        SELECT user_id, '默认', image_base64, created_at, updated_at
        FROM face_profiles
        """
    )


def downgrade() -> None:
    op.drop_index(op.f("ix_face_profile_entries_user_id"), table_name="face_profile_entries")
    op.drop_table("face_profile_entries")
