"""Retain an explicit not-started record even when cache/bootstrap admission refuses."""
import hashlib,json,os
from pathlib import Path
HERE=Path(__file__).resolve().parent
assert os.environ['GITHUB_RUN_ATTEMPT']=='1','This one-shot capsule refuses reruns'
manifest=json.loads((HERE/'manifest.json').read_bytes())
for name,expected in manifest['payload_sha256'].items():
 assert hashlib.sha256((HERE/name).read_bytes()).hexdigest()==expected,name
out=HERE/'measurement';out.mkdir()
with (out/'bootstrap.json').open('x') as f:
 json.dump({'status':'products_and_pair_not_started','capsule_commit':os.environ['GITHUB_SHA'],'source_commits':manifest['source_commits'],'diagnostic_only':True,'full_R17_acceptance':False,'SUT_invocations':0,'manifest_sha256':hashlib.sha256((HERE/'manifest.json').read_bytes()).hexdigest()},f,indent=2);f.write('\n')
