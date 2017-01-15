<?xml version="1.0" encoding="ISO-8859-1"?>
<xsl:transform version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                             xmlns:vty="urn:osmocom:xml:libosmocore:vty:doc:1.0">
  <xsl:output method="xml" version="1.0" encoding="UTF-8" indent="yes" />


  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()" />
    </xsl:copy>
  </xsl:template>


  <!-- Copy the name of the node -->
  <xsl:template match="vty:node">
    <xsl:variable name="info" select="document($with)/vty:vtydoc/vty:node[@id=current()/@id]/." />
    <xsl:if test="not($info/vty:hide)">
      <xsl:copy>
        <xsl:apply-templates select="@*|node()" />
          <xsl:for-each select="$info/*">
            <xsl:if test="not($info/vty:description)">
              <xsl:copy-of select="." />
	    </xsl:if>
          </xsl:for-each>
      </xsl:copy>
    </xsl:if>
  </xsl:template>


  <!-- Copy command and add nodes -->
  <xsl:template match="vty:command">
    <xsl:variable name="info" select="document($with)/vty:vtydoc/vty:node[@id=current()/../@id]/vty:command[@id=current()/@id]/." />
    <xsl:variable name="info_generic" select="document($with)/vty:vtydoc/vty:common/vty:command[@id=current()/@id]/." />
    <xsl:copy>
      <xsl:apply-templates select="@*|node()" />

      <!-- Copy the specific issue... -->
      <xsl:for-each select="$info/*">
        <xsl:copy-of select="." />
      </xsl:for-each>

      <xsl:if test="not($info)">
        <xsl:for-each select="$info_generic/*">
            <xsl:copy-of select="." />
        </xsl:for-each>
      </xsl:if>
    </xsl:copy>
  </xsl:template>
</xsl:transform>

