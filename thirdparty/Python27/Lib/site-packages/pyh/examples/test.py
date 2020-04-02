#!/usr/bin/env python
from pyh import *
page = PyH('My wonderful PyH page')
page.addCSS('myStylesheet1.css', 'myStylesheet2.css')
page.addJS('myJavascript1.js', 'myJavascript2.js')
page << h1(cl='center', txt='My big title')
page << div(cl='myCSSclass1 myCSSclass2', id='myDiv1') << p(id='myP1', txt='I love PyH!')
mydiv2 = page << div(id='myDiv2')
mydiv2 << h2('A smaller title') + p('Followed by a paragraph.')
page << div(id='myDiv3')
page.myDiv3.attributes['cl']='myCSSclass3'
page.myDiv3 << p('Another paragraph')
page.printOut()
