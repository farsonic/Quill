#!/usr/bin/env python3
# Makes tests/data/charts.docx (WP15a): a Word-style document with charts (c:chartSpace with cached
# data, embedded workbooks, chart colour and style parts), a chartex funnel in mc:AlternateContent, and a
# SmartArt (data, layout, quickStyle, colors, drawing). Needs openpyxl. Usage:
#   python3 tools/mkcharts.py out.docx [theme1.xml]   (the theme: tests/data/word.docx's word/theme/theme1.xml)
import io, sys, zipfile, openpyxl

out = sys.argv[1] if len(sys.argv) > 1 else "charts.docx"
theme = open(sys.argv[2], "rb").read() if len(sys.argv) > 2 else None

C = 'xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'
HDR = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\r\n'

def xlsx(cats, series):
    wb = openpyxl.Workbook(); ws = wb.active
    ws.cell(1, 1, "")
    for j, (n, v) in enumerate(series): ws.cell(1, j + 2, n)
    for i, c in enumerate(cats):
        ws.cell(i + 2, 1, c)
        for j, (n, v) in enumerate(series): ws.cell(i + 2, j + 2, v[i])
    b = io.BytesIO(); wb.save(b); return b.getvalue()

def strcache(ref, vals):
    s = '<c:strRef><c:f>%s</c:f><c:strCache><c:ptCount val="%d"/>' % (ref, len(vals))
    for i, v in enumerate(vals): s += '<c:pt idx="%d"><c:v>%s</c:v></c:pt>' % (i, v)
    return s + '</c:strCache></c:strRef>'

def numcache(ref, vals, fmt="General"):
    s = '<c:numRef><c:f>%s</c:f><c:numCache><c:formatCode>%s</c:formatCode><c:ptCount val="%d"/>' % (ref, fmt, len(vals))
    for i, v in enumerate(vals): s += '<c:pt idx="%d"><c:v>%s</c:v></c:pt>' % (i, v)
    return s + '</c:numCache></c:numRef>'

COLS = "BCDEFG"
def chart(kind, title, cats, series, extra="", legend="b", xy=False):
    n = len(cats)
    s = HDR + '<c:chartSpace %s><c:date1904 val="0"/><c:lang val="en-US"/><c:roundedCorners val="0"/><c:chart>' % C
    if title:
        s += '<c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>%s</a:t></a:r></a:p></c:rich></c:tx><c:overlay val="0"/></c:title>' % title
    s += '<c:autoTitleDeleted val="%d"/><c:plotArea><c:layout/>' % (0 if title else 1)
    tag = {"col": "barChart", "bar": "barChart", "line": "lineChart", "pie": "pieChart", "area": "areaChart",
           "doughnut": "doughnutChart", "scatter": "scatterChart", "radar": "radarChart", "surface": "surfaceChart", "bubble": "bubbleChart"}[kind]
    s += '<c:%s>' % tag
    if kind in ("col", "bar"): s += '<c:barDir val="%s"/><c:grouping val="clustered"/>' % ("col" if kind == "col" else "bar")
    if kind in ("line", "area"): s += '<c:grouping val="standard"/>'
    if kind == "scatter": s += '<c:scatterStyle val="lineMarker"/>'
    if kind == "radar": s += '<c:radarStyle val="marker"/>'
    s += '<c:varyColors val="%d"/>' % (1 if kind in ("pie", "doughnut") else 0)
    for k, (name, vals) in enumerate(series):
        col = COLS[k]
        s += '<c:ser><c:idx val="%d"/><c:order val="%d"/><c:tx>%s</c:tx>' % (k, k, strcache("Sheet1!$%s$1" % col, [name]))
        s += '<c:spPr><a:solidFill><a:schemeClr val="accent%d"/></a:solidFill></c:spPr>' % (k + 1)
        if xy:
            s += '<c:xVal>%s</c:xVal><c:yVal>%s</c:yVal>' % (numcache("Sheet1!$A$2:$A$%d" % (n + 1), cats), numcache("Sheet1!$%s$2:$%s$%d" % (col, col, n + 1), vals))
        else:
            s += '<c:cat>%s</c:cat><c:val>%s</c:val>' % (strcache("Sheet1!$A$2:$A$%d" % (n + 1), cats), numcache("Sheet1!$%s$2:$%s$%d" % (col, col, n + 1), vals))
        if kind == "line": s += '<c:smooth val="0"/>'
        s += '</c:ser>'
    s += extra
    if kind == "doughnut": s += '<c:firstSliceAng val="0"/><c:holeSize val="50"/>'
    if kind in ("pie", "doughnut"):
        s += '</c:%s>' % tag
    else:
        s += '<c:axId val="1001"/><c:axId val="1002"/></c:%s>' % tag
        ca = "valAx" if xy else "catAx"
        s += '<c:%s><c:axId val="1001"/><c:scaling><c:orientation val="minMax"/></c:scaling><c:delete val="0"/><c:axPos val="%s"/><c:crossAx val="1002"/></c:%s>' % (ca, "l" if kind == "bar" else "b", ca)
        s += '<c:valAx><c:axId val="1002"/><c:scaling><c:orientation val="minMax"/></c:scaling><c:delete val="0"/><c:axPos val="%s"/><c:majorGridlines/><c:numFmt formatCode="General" sourceLinked="1"/><c:crossAx val="1001"/></c:valAx>' % ("b" if kind == "bar" else "l")
    s += '</c:plotArea>'
    if legend: s += '<c:legend><c:legendPos val="%s"/><c:overlay val="0"/></c:legend>' % legend
    s += '<c:plotVisOnly val="1"/><c:dispBlanksAs val="gap"/></c:chart>'
    s += '<c:txPr><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr/></a:pPr><a:endParaRPr lang="en-US"/></a:p></c:txPr>'
    s += '<c:externalData r:id="rId3"><c:autoUpdate val="0"/></c:externalData></c:chartSpace>'
    return s.encode()

COLORS = (HDR + '<cs:colorStyle xmlns:cs="http://schemas.microsoft.com/office/drawing/2012/chartStyle" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" meth="cycle" id="10">'
          '<a:schemeClr val="accent1"/><a:schemeClr val="accent2"/><a:schemeClr val="accent3"/><a:schemeClr val="accent4"/><a:schemeClr val="accent5"/><a:schemeClr val="accent6"/>'
          '<cs:variation/><cs:variation><a:lumMod val="60000"/></cs:variation></cs:colorStyle>').encode()
STYLE = (HDR + '<cs:chartStyle xmlns:cs="http://schemas.microsoft.com/office/drawing/2012/chartStyle" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" id="201">'
         '<cs:axisTitle><cs:lnRef idx="0"/><cs:fillRef idx="0"/><cs:effectRef idx="0"/><cs:fontRef idx="minor"><a:schemeClr val="tx1"/></cs:fontRef></cs:axisTitle>'
         '</cs:chartStyle>').encode()

def chart_rels(xl):
    return (HDR + '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/package" Target="../embeddings/%s"/>'
            '<Relationship Id="rId2" Type="http://schemas.microsoft.com/office/2011/relationships/chartColorStyle" Target="colors%s"/>'
            '<Relationship Id="rId1" Type="http://schemas.microsoft.com/office/2011/relationships/chartStyle" Target="style%s"/>'
            '</Relationships>') % xl

def drawing(rid, uri, inner, cx, cy, n, anchor=None):
    g = ('<wp:extent cx="%d" cy="%d"/><wp:effectExtent l="0" t="0" r="0" b="0"/>%s<wp:docPr id="%d" name="Chart %d"/><wp:cNvGraphicFramePr/>'
         '<a:graphic xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"><a:graphicData uri="%s">%s</a:graphicData></a:graphic>') % (
        cx, cy, '<wp:wrapSquare wrapText="bothSides"/>' if anchor else "", n, n, uri, inner)
    if anchor:
        return ('<w:r><w:drawing><wp:anchor distT="0" distB="0" distL="114300" distR="114300" simplePos="0" relativeHeight="251659264" behindDoc="0" locked="0" layoutInCell="1" allowOverlap="1">'
                '<wp:simplePos x="0" y="0"/><wp:positionH relativeFrom="column"><wp:align>right</wp:align></wp:positionH><wp:positionV relativeFrom="paragraph"><wp:posOffset>63500</wp:posOffset></wp:positionV>'
                + g + '</wp:anchor></w:drawing></w:r>')
    return '<w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0">' + g + '</wp:inline></w:drawing></w:r>'

CH = "http://schemas.openxmlformats.org/drawingml/2006/chart"
def cref(rid): return '<c:chart xmlns:c="%s" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" r:id="%s"/>' % (CH, rid)

cats = ["Q1", "Q2", "Q3", "Q4"]
ser = [("North", [4.3, 2.5, 3.5, 4.5]), ("South", [2.4, 4.4, 1.8, 2.8]), ("West", [2, 2, 3, 5])]
charts = [
    ("col", "Sales by Quarter", cats, ser, "b", False),
    ("pie", "Share", ["Apples", "Pears", "Plums", "Figs"], [("Fruit", [8.2, 3.2, 1.4, 1.2])], "r", False),
    ("line", "Trend", cats, ser[:2], "b", False),
    ("bar", "", cats, ser[:2], "r", False),
    ("area", "Area", cats, ser[:2], "t", False),
    ("doughnut", "Ring", ["A", "B", "C"], [("S", [5, 3, 2])], "r", False),
    ("scatter", "Scatter", ["1", "2", "3", "4", "5"], [("Y", [2.0, 3.5, 1.5, 4.2, 3.9])], "b", True),
    ("radar", "Radar", ["Speed", "Power", "Range", "Cost", "Comfort"], [("Car A", [4, 3, 5, 2, 4]), ("Car B", [3, 5, 2, 4, 3])], "b", False),
    ("surface", "Surface (placeholder)", cats, ser[:2], "b", False),
]
files = {}
ctypes = []
docrels = []
body = '<w:p><w:r><w:t>Charts kept by Quill. An inline column chart:</w:t></w:r></w:p>'
for i, (kind, title, ca, se, lg, xy) in enumerate(charts, 1):
    files["word/charts/chart%d.xml" % i] = chart(kind, title, ca, se, legend=lg, xy=xy)
    xl = "Microsoft_Excel_Worksheet%s.xlsx" % ("" if i == 1 else str(i - 1))
    files["word/embeddings/" + xl] = xlsx(ca, se)
    files["word/charts/colors%d.xml" % i] = COLORS
    files["word/charts/style%d.xml" % i] = STYLE
    files["word/charts/_rels/chart%d.xml.rels" % i] = (chart_rels((xl, "%d.xml" % i, "%d.xml" % i))).encode()
    ctypes.append('<Override PartName="/word/charts/chart%d.xml" ContentType="application/vnd.openxmlformats-officedocument.drawingml.chart+xml"/>' % i)
    ctypes.append('<Override PartName="/word/charts/colors%d.xml" ContentType="application/vnd.ms-office.chartcolorstyle+xml"/>' % i)
    ctypes.append('<Override PartName="/word/charts/style%d.xml" ContentType="application/vnd.ms-office.chartstyle+xml"/>' % i)
    rid = "rId%d" % (10 + i)
    docrels.append('<Relationship Id="%s" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart" Target="charts/chart%d.xml"/>' % (rid, i))
    if i == 2:
        body += '<w:p>%s<w:r><w:t>A floating pie chart wrapped square at the right. %s</w:t></w:r></w:p>' % (drawing(rid, CH, cref(rid), 2743200, 1828800, i, anchor=True), "Text flows round it. " * 12)
    else:
        body += '<w:p>%s</w:p>' % drawing(rid, CH, cref(rid), 3657600, 2011680, i)

# a chartex funnel, as Word writes it: mc:AlternateContent with a fallback text box
CX = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\r\n<cx:chartSpace xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
      'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:cx="http://schemas.microsoft.com/office/drawing/2014/chartex">'
      '<cx:chartData><cx:externalData r:id="rId1" cx:autoUpdate="0"/><cx:data id="0"><cx:strDim type="cat"><cx:f>Sheet1!$A$2:$A$5</cx:f><cx:lvl ptCount="4">'
      '<cx:pt idx="0">Leads</cx:pt><cx:pt idx="1">Calls</cx:pt><cx:pt idx="2">Offers</cx:pt><cx:pt idx="3">Deals</cx:pt></cx:lvl></cx:strDim>'
      '<cx:numDim type="val"><cx:f>Sheet1!$B$2:$B$5</cx:f><cx:lvl ptCount="4" formatCode="General"><cx:pt idx="0">120</cx:pt><cx:pt idx="1">80</cx:pt><cx:pt idx="2">40</cx:pt><cx:pt idx="3">15</cx:pt></cx:lvl></cx:numDim></cx:data></cx:chartData>'
      '<cx:chart><cx:title pos="t" align="ctr" overlay="0"><cx:tx><cx:txData><cx:v>Funnel</cx:v></cx:txData></cx:tx></cx:title><cx:plotArea><cx:plotAreaRegion>'
      '<cx:series layoutId="funnel" uniqueId="{00000000-0001-0000-0000-000000000000}"><cx:tx><cx:txData><cx:f>Sheet1!$B$1</cx:f><cx:v>Pipeline</cx:v></cx:txData></cx:tx>'
      '<cx:dataId val="0"/></cx:series></cx:plotAreaRegion></cx:plotArea></cx:chart></cx:chartSpace>').encode()
files["word/charts/chartEx1.xml"] = CX
files["word/embeddings/Microsoft_Excel_Worksheet_cx.xlsx"] = xlsx(["Leads", "Calls", "Offers", "Deals"], [("Pipeline", [120, 80, 40, 15])])
files["word/charts/_rels/chartEx1.xml.rels"] = (HDR + '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
    '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/package" Target="../embeddings/Microsoft_Excel_Worksheet_cx.xlsx"/></Relationships>').encode()
ctypes.append('<Override PartName="/word/charts/chartEx1.xml" ContentType="application/vnd.ms-office.chartex+xml"/>')
docrels.append('<Relationship Id="rId30" Type="http://schemas.microsoft.com/office/2014/relationships/chartEx" Target="charts/chartEx1.xml"/>')
cxinner = '<cx:chart xmlns:cx="http://schemas.microsoft.com/office/drawing/2014/chartex" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" r:id="rId30"/>'
body += ('<w:p><w:r><mc:AlternateContent xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"><mc:Choice xmlns:cx1="http://schemas.microsoft.com/office/drawing/2015/9/8/chartex" Requires="cx1">'
         + drawing("rId30", "http://schemas.microsoft.com/office/drawing/2014/chartex", cxinner, 3657600, 2011680, 20)[5:-6]
         + '</mc:Choice><mc:Fallback><w:t>This chart isn\'t available in your version of Word.</w:t></mc:Fallback></mc:AlternateContent></w:r></w:p>')

# a SmartArt: a basic process of three steps, with the drawing Word saves beside its data
DGM = 'xmlns:dgm="http://schemas.openxmlformats.org/drawingml/2006/diagram" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'
steps = ["Plan", "Build", "Ship"]
data = HDR + '<dgm:dataModel %s><dgm:ptLst><dgm:pt modelId="{0}" type="doc"><dgm:prSet loTypeId="urn:microsoft.com/office/officeart/2005/8/layout/process1" loCatId="process" qsTypeId="urn:microsoft.com/office/officeart/2005/8/quickstyle/simple1" qsCatId="simple" csTypeId="urn:microsoft.com/office/officeart/2005/8/colors/accent1_2" csCatId="accent1"/><dgm:spPr/><dgm:t><a:bodyPr/><a:lstStyle/><a:p><a:endParaRPr lang="en-US"/></a:p></dgm:t></dgm:pt>' % DGM
for i, s in enumerate(steps, 1):
    data += '<dgm:pt modelId="{%d}"><dgm:prSet phldrT="[Text]"/><dgm:spPr/><dgm:t><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US"/><a:t>%s</a:t></a:r></a:p></dgm:t></dgm:pt>' % (i, s)
data += '</dgm:ptLst><dgm:cxnLst>'
for i in range(1, 4):
    data += '<dgm:cxn modelId="{%d}" srcId="{0}" destId="{%d}" srcOrd="%d" destOrd="0"/>' % (10 + i, i, i - 1)
data += ('</dgm:cxnLst><dgm:bg/><dgm:whole/><dgm:extLst><a:ext uri="http://schemas.microsoft.com/office/drawing/2008/diagram">'
         '<dsp:dataModelExt xmlns:dsp="http://schemas.microsoft.com/office/drawing/2008/diagram" relId="rId44" minVer="http://schemas.openxmlformats.org/drawingml/2006/diagram"/></a:ext></dgm:extLst></dgm:dataModel>')
files["word/diagrams/data1.xml"] = data.encode()
files["word/diagrams/layout1.xml"] = (HDR + '<dgm:layoutDef %s uniqueId="urn:microsoft.com/office/officeart/2005/8/layout/process1"><dgm:title val=""/><dgm:desc val=""/><dgm:catLst><dgm:cat type="process" pri="1000"/></dgm:catLst>'
    '<dgm:layoutNode name="Name0"><dgm:varLst><dgm:dir/><dgm:resizeHandles val="exact"/></dgm:varLst><dgm:alg type="lin"/><dgm:shape xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" r:blip=""><dgm:adjLst/></dgm:shape><dgm:presOf/>'
    '<dgm:forEach name="Name1" axis="ch" ptType="node"><dgm:layoutNode name="node"><dgm:alg type="tx"/><dgm:shape type="roundRect" r:blip=""><dgm:adjLst/></dgm:shape><dgm:presOf axis="desOrSelf" ptType="node"/></dgm:layoutNode></dgm:forEach></dgm:layoutNode></dgm:layoutDef>' % DGM).encode()
files["word/diagrams/quickStyle1.xml"] = (HDR + '<dgm:styleDef %s uniqueId="urn:microsoft.com/office/officeart/2005/8/quickstyle/simple1"><dgm:title val=""/><dgm:desc val=""/><dgm:catLst><dgm:cat type="simple" pri="10100"/></dgm:catLst>'
    '<dgm:scene3d><a:camera prst="orthographicFront"/><a:lightRig rig="threePt" dir="t"/></dgm:scene3d><dgm:styleLbl name="node1"><dgm:scene3d><a:camera prst="orthographicFront"/><a:lightRig rig="threePt" dir="t"/></dgm:scene3d><dgm:sp3d/><dgm:txPr/><dgm:style><a:lnRef idx="2"><a:scrgbClr r="0" g="0" b="0"/></a:lnRef><a:fillRef idx="1"><a:scrgbClr r="0" g="0" b="0"/></a:fillRef><a:effectRef idx="0"><a:scrgbClr r="0" g="0" b="0"/></a:effectRef><a:fontRef idx="minor"><a:schemeClr val="lt1"/></a:fontRef></dgm:style></dgm:styleLbl></dgm:styleDef>' % DGM).encode()
files["word/diagrams/colors1.xml"] = (HDR + '<dgm:colorsDef %s uniqueId="urn:microsoft.com/office/officeart/2005/8/colors/accent1_2"><dgm:title val=""/><dgm:desc val=""/><dgm:catLst><dgm:cat type="accent1" pri="11200"/></dgm:catLst>'
    '<dgm:styleLbl name="node1"><dgm:fillClrLst meth="repeat"><a:schemeClr val="accent1"/></dgm:fillClrLst><dgm:linClrLst meth="repeat"><a:schemeClr val="lt1"/></dgm:linClrLst><dgm:effectClrLst/><dgm:txLinClrLst/><dgm:txFillClrLst meth="repeat"><a:schemeClr val="lt1"/></dgm:txFillClrLst><dgm:txEffectClrLst/></dgm:styleLbl></dgm:colorsDef>' % DGM).encode()
dsp = (HDR + '<dsp:drawing xmlns:dgm="http://schemas.openxmlformats.org/drawingml/2006/diagram" xmlns:dsp="http://schemas.microsoft.com/office/drawing/2008/diagram" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">'
       '<dsp:spTree><dsp:nvGrpSpPr><dsp:cNvPr id="0" name=""/><dsp:cNvGrpSpPr/></dsp:nvGrpSpPr><dsp:grpSpPr/>')
W, H = 1371600, 822960
for i, s in enumerate(steps):
    x = i * (W + 548640)
    dsp += ('<dsp:sp modelId="{%d}"><dsp:nvSpPr><dsp:cNvPr id="0" name=""/><dsp:cNvSpPr/></dsp:nvSpPr><dsp:spPr><a:xfrm><a:off x="%d" y="0"/><a:ext cx="%d" cy="%d"/></a:xfrm>'
            '<a:prstGeom prst="roundRect"><a:avLst><a:gd name="adj" fmla="val 10000"/></a:avLst></a:prstGeom><a:solidFill><a:schemeClr val="accent1"><a:hueOff val="0"/></a:schemeClr></a:solidFill>'
            '<a:ln w="12700"><a:solidFill><a:schemeClr val="lt1"/></a:solidFill></a:ln></dsp:spPr><dsp:style><a:lnRef idx="2"><a:scrgbClr r="0" g="0" b="0"/></a:lnRef><a:fillRef idx="1"><a:scrgbClr r="0" g="0" b="0"/></a:fillRef>'
            '<a:effectRef idx="0"><a:scrgbClr r="0" g="0" b="0"/></a:effectRef><a:fontRef idx="minor"><a:schemeClr val="lt1"/></a:fontRef></dsp:style>'
            '<dsp:txBody><a:bodyPr spcFirstLastPara="0" vert="horz" wrap="square" lIns="76200" tIns="76200" rIns="76200" bIns="76200" numCol="1" spcCol="1270" anchor="ctr" anchorCtr="0"><a:noAutofit/></a:bodyPr><a:lstStyle/>'
            '<a:p><a:pPr marL="0" lvl="0" indent="0" algn="ctr" defTabSz="1333500"><a:lnSpc><a:spcPct val="90000"/></a:lnSpc><a:spcBef><a:spcPct val="0"/></a:spcBef><a:spcAft><a:spcPct val="35000"/></a:spcAft></a:pPr>'
            '<a:r><a:rPr lang="en-US" sz="2400" kern="1200"/><a:t>%s</a:t></a:r></a:p></dsp:txBody><dsp:txXfrm><a:off x="%d" y="24104"/><a:ext cx="%d" cy="%d"/></dsp:txXfrm></dsp:sp>') % (i + 1, x, W, H, s, x + 24104, W - 48208, H - 48208)
    if i < 2:
        ax = x + W + 137160
        dsp += ('<dsp:sp modelId="{%d}"><dsp:nvSpPr><dsp:cNvPr id="0" name=""/><dsp:cNvSpPr/></dsp:nvSpPr><dsp:spPr><a:xfrm><a:off x="%d" y="241782"/><a:ext cx="290779" cy="340156"/></a:xfrm>'
                '<a:prstGeom prst="rightArrow"><a:avLst><a:gd name="adj1" fmla="val 60000"/><a:gd name="adj2" fmla="val 50000"/></a:avLst></a:prstGeom>'
                '<a:solidFill><a:schemeClr val="accent1"><a:tint val="60000"/></a:schemeClr></a:solidFill><a:ln><a:noFill/></a:ln></dsp:spPr></dsp:sp>') % (20 + i, ax)
dsp += '</dsp:spTree></dsp:drawing>'
files["word/diagrams/drawing1.xml"] = dsp.encode()
for p, t in (("data1", "diagramData"), ("layout1", "diagramLayout"), ("quickStyle1", "diagramStyle"), ("colors1", "diagramColors")):
    ctypes.append('<Override PartName="/word/diagrams/%s.xml" ContentType="application/vnd.openxmlformats-officedocument.drawingml.%s+xml"/>' % (p, t))
ctypes.append('<Override PartName="/word/diagrams/drawing1.xml" ContentType="application/vnd.ms-office.drawingml.diagramDrawing+xml"/>')
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
docrels += ['<Relationship Id="rId40" Type="%sdiagramData" Target="diagrams/data1.xml"/>' % R,
            '<Relationship Id="rId41" Type="%sdiagramLayout" Target="diagrams/layout1.xml"/>' % R,
            '<Relationship Id="rId42" Type="%sdiagramQuickStyle" Target="diagrams/quickStyle1.xml"/>' % R,
            '<Relationship Id="rId43" Type="%sdiagramColors" Target="diagrams/colors1.xml"/>' % R,
            '<Relationship Id="rId44" Type="http://schemas.microsoft.com/office/2007/relationships/diagramDrawing" Target="diagrams/drawing1.xml"/>']
dg = '<dgm:relIds xmlns:dgm="http://schemas.openxmlformats.org/drawingml/2006/diagram" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" r:dm="rId40" r:lo="rId41" r:qs="rId42" r:cs="rId43"/>'
body += '<w:p><w:r><w:t>A SmartArt process:</w:t></w:r></w:p><w:p>%s</w:p>' % drawing("rId40", "http://schemas.openxmlformats.org/drawingml/2006/diagram", dg, 5486400, 822960, 30)
body += '<w:p><w:r><w:t>The end.</w:t></w:r></w:p>'

doc = (HDR + '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
       'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
       'xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"><w:body>' + body +
       '<w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/></w:sectPr></w:body></w:document>')
files["word/document.xml"] = doc.encode()
if theme:
    files["word/theme/theme1.xml"] = theme
    docrels.append('<Relationship Id="rId9" Type="%stheme" Target="theme/theme1.xml"/>' % R)
    ctypes.append('<Override PartName="/word/theme/theme1.xml" ContentType="application/vnd.openxmlformats-officedocument.theme+xml"/>')
files["word/_rels/document.xml.rels"] = (HDR + '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">' + "".join(docrels) + '</Relationships>').encode()
files["_rels/.rels"] = (HDR + '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="%sofficeDocument" Target="word/document.xml"/></Relationships>' % R).encode()
files["[Content_Types].xml"] = (HDR + '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
    '<Default Extension="xml" ContentType="application/xml"/><Default Extension="xlsx" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"/>'
    '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>' + "".join(ctypes) + '</Types>').encode()
order = ["[Content_Types].xml", "_rels/.rels", "word/document.xml", "word/_rels/document.xml.rels"]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for n in order + sorted(k for k in files if k not in order):
        z.writestr(n, files[n])
print("wrote", out)
