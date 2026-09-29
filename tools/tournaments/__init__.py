"""Standard-library experiment execution and analysis for Globulation 2."""
from .model import PROTOCOL_VERSION, grid, seeded_samples, job, validate_experiment
from .results import Results

__all__ = ['PROTOCOL_VERSION', 'grid', 'seeded_samples', 'job', 'validate_experiment', 'Results']
