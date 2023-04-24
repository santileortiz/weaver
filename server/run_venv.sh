python3 -m venv myenv
source myenv/bin/activate
pip3 install gunicorn
pip3 install dist/app_dist-0.1-py3-none-any.whl
gunicorn weaver.main:app --bind 0.0.0.0:9000
rm -r myenv
