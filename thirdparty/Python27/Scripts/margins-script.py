#!C:\Python27\python.exe
# EASY-INSTALL-ENTRY-SCRIPT: 'xlutils==2.0.0','console_scripts','margins'
__requires__ = 'xlutils==2.0.0'
import re
import sys
from pkg_resources import load_entry_point

if __name__ == '__main__':
    sys.argv[0] = re.sub(r'(-script\.pyw?|\.exe)?$', '', sys.argv[0])
    sys.exit(
        load_entry_point('xlutils==2.0.0', 'console_scripts', 'margins')()
    )
