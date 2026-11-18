#!/usr/bin/env python3
# tools/mkstrict.py - makes tests/data/strict-*.docx, ISO 29500 Strict Open XML copies of Quill's own test
# documents, for tests/test_wp28e.c (WP28e): LibreOffice can't write Strict, so the Transitional
# namespaces are swapped for the Strict ones (parts and relationship types), the document is marked
# w:conformance="strict", and some values are written as Strict writes them: measures with units
# (pt, in, cm, mm), percentages, on/off as true/false, start/end for left/right.
#   python3 tools/mkstrict.py            (from the repository root)
import re, sys, zipfile

NS = [
    ('http://schemas.openxmlformats.org/wordprocessingml/2006/main', 'http://purl.oclc.org/ooxml/wordprocessingml/main'),
    ('http://schemas.openxmlformats.org/officeDocument/2006/relationships', 'http://purl.oclc.org/ooxml/officeDocument/relationships'),
    ('http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing', 'http://purl.oclc.org/ooxml/drawingml/wordprocessingDrawing'),
    ('http://schemas.openxmlformats.org/drawingml/2006/main', 'http://purl.oclc.org/ooxml/drawingml/main'),
    ('http://schemas.openxmlformats.org/drawingml/2006/picture', 'http://purl.oclc.org/ooxml/drawingml/picture'),
    ('http://schemas.openxmlformats.org/officeDocument/2006/math', 'http://purl.oclc.org/ooxml/officeDocument/math'),
]

def twips_to(v, unit):
    t = int(v)
    if unit == 'pt': return ('%g' % (t / 20)) + 'pt'
    if unit == 'in': return ('%g' % (t / 1440)) + 'in'
    if unit == 'cm': return ('%.3f' % (t / 566.929)).rstrip('0').rstrip('.') + 'cm'
    return ('%.2f' % (t / 56.6929)).rstrip('0').rstrip('.') + 'mm'

def strict_doc(x):
    x = x.replace('<w:document ', '<w:document w:conformance="strict" ', 1)
    # page size and margins in inches and points, spacing in points, indents in cm
    x = re.sub(r'(<w:pgSz [^>]*?w:w=")(\d+)', lambda m: m.group(1) + twips_to(m.group(2), 'in'), x)
    x = re.sub(r'(<w:pgSz [^>]*?w:h=")(\d+)', lambda m: m.group(1) + twips_to(m.group(2), 'in'), x)
    x = re.sub(r'(<w:pgMar [^>]*?w:top=")(\d+)', lambda m: m.group(1) + twips_to(m.group(2), 'pt'), x)
    x = re.sub(r'(<w:pgMar [^>]*?w:left=")(\d+)', lambda m: m.group(1) + twips_to(m.group(2), 'mm'), x)
    x = re.sub(r'(<w:spacing [^>]*?w:after=")(\d+)', lambda m: m.group(1) + twips_to(m.group(2), 'pt'), x)
    x = re.sub(r'(<w:ind [^>]*?)w:left="(\d+)', lambda m: m.group(1) + 'w:start="' + twips_to(m.group(2), 'cm'), x)
    x = re.sub(r'<w:jc w:val="left"/>', '<w:jc w:val="start"/>', x)
    x = re.sub(r'<w:jc w:val="right"/>', '<w:jc w:val="end"/>', x)
    x = re.sub(r'<w:sz w:val="(\d+)"/>', lambda m: '<w:sz w:val="%gpt"/>' % (int(m.group(1)) / 2), x)
    x = re.sub(r'(<w:t[cb]lW w:w=")(\d+)(" w:type="pct")', lambda m: m.group(1) + '%g%%' % (int(m.group(2)) / 50) + m.group(3), x)
    x = re.sub(r'w:val="(on|1)"', 'w:val="true"', x)
    x = re.sub(r'w:val="(off|0)"(?=/><w:(b|i|caps|strike)\b)', 'w:val="false"', x)
    return x

def make(src, dst):
    zin = zipfile.ZipFile(src)
    zout = zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED)
    for it in zin.infolist():
        data = zin.read(it.filename)
        if it.filename.endswith('.xml') or it.filename.endswith('.rels'):
            x = data.decode('utf-8')
            for a, b in NS: x = x.replace(a, b)
            if it.filename == 'word/document.xml': x = strict_doc(x)
            elif it.filename == 'word/styles.xml':
                x = re.sub(r'<w:sz w:val="(\d+)"/>', lambda m: '<w:sz w:val="%gpt"/>' % (int(m.group(1)) / 2), x)
            data = x.encode('utf-8')
        zout.writestr(it, data)
    zout.close()

for name in ('word', 'tables', 'pictures'):
    make('tests/data/%s.docx' % name, 'tests/data/strict-%s.docx' % name)
    print('tests/data/strict-%s.docx' % name)
