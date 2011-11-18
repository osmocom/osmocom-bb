<?xml version="1.0" encoding="ISO-8859-1"?>
<xsl:transform version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                             xmlns:vty="urn:osmocom:xml:libosmocore:vty:doc:1.0">
  <xsl:output method="xml" version="1.0" encoding="ISO-8859-1" indent="yes" />
  <xsl:variable name="with" select="'additions.xml'" />

  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()" />
    </xsl:copy>
  </xsl:template>


  <!-- Copy the name of the node -->
  <xsl:template match="vty:node">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()" />
      <xsl:variable name="info" select="document($with)/vty:vtydoc/vty:node[@id=current()/@id]/." />
      <xsl:for-each select="$info/vty:name">
          <xsl:copy-of select="." />
      </xsl:for-each>
    </xsl:copy>
  </xsl:template>


  <!-- Copy command and add nodes -->
  <xsl:template match="vty:command">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()" />
      <xsl:variable name="info" select="document($with)/vty:vtydoc/vty:node[@id=current()/../@id]/vty:command[@id=current()/@id]/." />
      <xsl:for-each select="$info/*">
          <xsl:copy-of select="." />
      </xsl:for-each>
    </xsl:copy>
  </xsl:template>
</xsl:transform>

