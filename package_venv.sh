python3 -m venv myenv
source myenv/bin/activate
pip3 install flask gunicorn wheel
python3 setup.py bdist_wheel
cp -r dist bin/
rm -r myenv build *.egg-info dist
