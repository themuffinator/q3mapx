"""Prepare first-party input and exercise the actual Qt process queue. GPL-3.0-or-later."""
import argparse
from pathlib import Path
import subprocess
from fixtures import create_fixture

parser=argparse.ArgumentParser()
parser.add_argument('--test',type=Path,required=True)
parser.add_argument('--compiler',type=Path,required=True)
parser.add_argument('--work-dir',type=Path,required=True)
args=parser.parse_args()
root=args.work_dir.resolve(); source=create_fixture(root/'fixture with spaces')
result=subprocess.run([str(args.test.resolve()),str(args.compiler.resolve()),str(source),str(root)],cwd=root,timeout=100,
                      stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(root/'workbench-test.log').write_bytes(result.stdout)
print(result.stdout.decode(errors='replace'))
raise SystemExit(result.returncode)
