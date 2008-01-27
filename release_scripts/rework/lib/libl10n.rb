#!/usr/bin/env ruby
#
# Generic ruby library for KDE extragear/playground releases
#
# Copyright (C) 2007-2008 Harald Sitter <harald@getamarok.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

require 'lib/libkdialog.rb'
require 'lib/libbase.rb'
require 'fileutils'

@dlg = KDialog.new("#{NAME} release script","cookie")

def FetchTranslations()
  bar  = @dlg.progressbar("preparing l10n processing",1)
  SrcDir()
  Dir.mkdir("l10n")
  Dir.mkdir("po")

  l10nlangs     = `svn cat #{@repo}/l10n-kde4/subdirs`.chomp!()
  @translations = []
  subdirs       = false

  bar.maxvalue = l10nlangs.count("\n")
  step         = 0

  for lang in l10nlangs
    lang.chomp!()
    bar.label    = "processing po/#{lang}"
    bar.progress = step
    step        += 1

    pofilename = "l10n-kde4/#{lang}/messages/#{COMPONENT}-#{SECTION}"
    # TODO: ruby-svn
    FileUtils.rm_rf("l10n")
    `svn co #{@repo}/#{pofilename} l10n 2> /dev/null`
    next unless FileTest.exists?( "l10n/#{NAME}.po" )

    dest = "po/#{lang}"
    Dir.mkdir( dest )
    puts "Copying #{lang}'s #{NAME}.po over ..."
    FileUtils.mv( "l10n/#{NAME}.po", dest )
    FileUtils.mv( "l10n/.svn", dest )

    # create lang's cmake files
    cmakefile = File.new( "#{dest}/CMakeLists.txt", File::CREAT | File::RDWR | File::TRUNC )
    cmakefile << "file(GLOB _po_files *.po)\n"
    cmakefile << "GETTEXT_PROCESS_PO_FILES(${CURRENT_LANG} ALL INSTALL_DESTINATION ${LOCALE_INSTALL_DIR} ${_po_files} )\n"
    cmakefile.close()

    # add to SVN in case we are tagging
    `svn add #{dest}/CMakeLists.txt`
    @translations += [lang]

    puts "done.\n"

    subdirs = true
  end
  bar.close

  if subdirs
    # create po's cmake file
    cmakefile = File.new( "po/CMakeLists.txt", File::CREAT | File::RDWR | File::TRUNC )
    cmakefile << "find_package(Gettext REQUIRED)\n"
    cmakefile << "if (NOT GETTEXT_MSGMERGE_EXECUTABLE)\n"
    cmakefile << "MESSAGE(FATAL_ERROR \"Please install msgmerge binary\")\n"
    cmakefile << "endif (NOT GETTEXT_MSGMERGE_EXECUTABLE)\n"
    cmakefile << "if (NOT GETTEXT_MSGFMT_EXECUTABLE)\n"
    cmakefile << "MESSAGE(FATAL_ERROR \"Please install msgmerge binary\")\n"
    cmakefile << "endif (NOT GETTEXT_MSGFMT_EXECUTABLE)\n"
    Dir.foreach( "po" ) {|lang|
      unless lang == ".." or lang == "." or lang == "CMakeLists.txt"
        cmakefile << "add_subdirectory(#{lang})\n"
      end
    }
    cmakefile.close()

    # adapt cmake file
    cmakefile = File.new( "CMakeLists.txt", File::APPEND | File::RDWR )
    cmakefile << "include(MacroOptionalAddSubdirectory)\n"
    cmakefile << "macro_optional_add_subdirectory( po )\n"
    cmakefile.close()
  else
    FileUtils.rm_rf( "po" )
  end

  FileUtils.rm_rf( "l10n" )
end


def FetchDocumentation()
  bar  = @dlg.progressbar("preparing doc processing",1)
  SrcDir()

  l10nlangs = `svn cat #{@repo}/l10n-kde4/subdirs`.chomp!()
  @docs     = []
  subdirs   = false

  bar.maxvalue = l10nlangs.count( "\n" )
  step         = 0

  `svn co #{@repo}/#{COMPONENT}/#{SECTION}/doc/#{NAME} doc/en_US`
  cmakefile = File.new( "doc/en_US/CMakeLists.txt", File::CREAT | File::RDWR | File::TRUNC )
  cmakefile << "kde4_create_handbook(index.docbook INSTALL_DESTINATION \${HTML_INSTALL_DIR}/\${CURRENT_LANG}/ SUBDIR #{NAME} )\n"
  cmakefile.close()

  # docs
  for lang in l10nlangs
    lang.chomp!()
    bar.label    = "processing #{lang}'s #{NAME} documentation"
    bar.progress = step
    step        += 1

    docdirname = "l10n-kde4/#{lang}/docs/#{COMPONENT}-#{SECTION}/#{NAME}"
    # TODO: ruby-svn
    FileUtils.rm_rf( "l10n" )
    `svn co #{@repo}/#{docdirname} l10n 2> /dev/null`
    puts "svn co #{@repo}/#{docdirname} l10n 2> /dev/null"
    next unless FileTest.exists?( "l10n" )

    dest = "doc/#{lang}"
    puts "Copying #{lang}'s #{NAME} documentation over... "
    FileUtils.mv( "l10n", dest )

    cmakefile = File.new( "doc/#{lang}/CMakeLists.txt", File::CREAT | File::RDWR | File::TRUNC )
    cmakefile << "kde4_create_handbook(index.docbook INSTALL_DESTINATION \${HTML_INSTALL_DIR}/\${CURRENT_LANG}/ SUBDIR #{NAME} )\n"
    cmakefile.close()

    # add to SVN in case we are tagging
    `svn add doc/#{lang}/CMakeLists.txt`
    @docs += [lang]

    puts( "done.\n" )

    subdirs = true
  end
  bar.close

  SrcDir()

  if subdirs
    # create doc's cmake file
    cmakefile = File.new( "doc/CMakeLists.txt", File::CREAT | File::RDWR | File::TRUNC )
    Dir.foreach( "doc" ) {|lang|
      unless lang == ".." or lang == "." or lang == "CMakeLists.txt"
        cmakefile << "add_subdirectory(#{lang})\n"
      end
    }
    cmakefile.close()

    # adapt cmake file
    cmakefile = File.new( "CMakeLists.txt", File::APPEND | File::RDWR )
    unless File.exists?( "po" )
      cmakefile << "include(MacroOptionalAddSubdirectory)\n"
    end
    cmakefile << "macro_optional_add_subdirectory( doc )\n"
    cmakefile.close()
  else
    FileUtils.rm_rf( "doc" )
  end

  FileUtils.rm_rf( "l10n" )
end


def CompressDocumentationImages()
end


# TODO: there are some rough edges :-S
def CreateTranslationStats()
  @clang    = -2 #current language
  @cfuzzy   = 0  #fuzzies
  @cuntrans = 0  #untranslated
  @cnotshow = 0  #all not-shown
  @cper     = 0  #percentage
  @file     = "../amarok-l10n-#{@version}.html"

  def CalcPercentage( per )
    unless @cper == "0"
      @cper = ((@cper + per) / 2)
    else
      @cper = per
    end
  end

  # write HTML header part
  def Header()
    `echo '
    <!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN"  "http://www.w3.org/TR/html4/loose.dtd">
    <html>
    <head>
    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
    <title>Statistics of Amarok #{@version} translations</title>
    </head>
    <body>
    <a name="__top"><p align="center"><a name="statistics of amarok #{@version} translations">
    <h1>Statistics of Amarok #{@version} translations</h1><br>
    <table border="1" cellspacing="0"dir="ltr">
    <tr><td align="left" valign="middle" width="60" height="12">
    <font color="#196aff"><i><b>Language</b></i></font>
    </td><td align="center" valign="middle" width="142" height="12">
    <font color="#196aff"><i><b>Fuzzy Strings</b></i></font>
    </td><td align="center" valign="middle" width="168" height="12">
    <font color="#196aff"><i><b>Untranslated Strings</b></i></font>
    </td><td align="center" valign="middle" width="163" height="12">
    <font color="#196aff"><i><b>All Not Shown Strings</b></i></font>
    </td><td align="center" valign="middle" width="163" height="12">
    <font color="#196aff"><i><b>Translated %</b></i></font>
    </td></tr>' > #{@file}`
  end

  # write HTML footer part
  def Footer()
    `echo "
    <tr><td align="left" valign="middle" width="60" height="12">
    <u><i><b>#{@clang}</b></i></u></td>
    <td align="center" valign="middle" width="142" height="12"><u><i><b>
    #{if @cfuzzy == "0" then "0" else @cfuzzy end}
    </b></i></u></td>
    <td align="center" valign="middle" width="168" height="12"><u><i><b>
    #{if @cuntrans == "0" then "0" else @cuntrans end}
    </b></i></u></td>
    <td align="center" valign="middle" width="163" height="12"><u><i><b>
    #{if @cnotshow == "0" then "0" else @cnotshow end}
    </b></i></u></td>
    <td align="center" valign="middle" width="163" height="12"><u><i><b>
    #{if @cper == "0" then "0" else @cper.to_s + " %" end}
    </b></i></u></td></tr>" >> #{@file}`
  end

  def Stats( lang )
    SrcDir()
    values = nil

    if lang != "." and lang != ".." and lang != "CMakeLists.txt" then
      Dir.chdir("po/#{lang}")

      # grab statistics data
      `msgfmt --statistics amarok.po 2> tmp.txt`
      term = `cat tmp.txt`
      File.delete("tmp.txt")

      # rape the data and create some proper variables
      values  = term.scan(/[\d]+/)
      notshow = values[1].to_i + values[2].to_i
      all     = values[0].to_i + values[1].to_i + values[2].to_i
      show    = all - notshow
      per     = ((100.0 * show.to_f) / all.to_f)

      # assign font colors according to translation status
# TODO: replace with case -> point out how to do with relational operators
      if per == 100 then
        fcolor = "#00B015" #green
      elsif per >= 95 then
        fcolor = "#FF9900" #orange
      elsif per >= 75 then
        fcolor = "#6600FF" #blue
      elsif per >= 50 then
        fcolor = "#000000" #black
      else
        fcolor = "#FF0000" #red
      end

      SrcDir()

      `echo "
      <tr><td align="left" valign="middle" width="60" height="12">
      <font color="#{fcolor}">
      #{lang}
      </font></td>
      <td align="center" valign="middle" width="142" height="12">
      <font color="#{fcolor}">
      #{if values[1] == nil then "0" else values[1] end}
      </font></td>
      <td align="center" valign="middle" width="168" height="12">
      <font color="#{fcolor}">
      #{if values[2] == nil then "0" else values[2] end}
      </font></td>
      <td align="center" valign="middle" width="163" height="12">
      <font color="#{fcolor}">
      #{if notshow == nil then "0" else notshow end}
      </font></td>
      <td align="center" valign="middle" width="163" height="12">
      <font color="#{fcolor}">
      #{if per == 0 then "0 %" else per.to_i.to_s + " %" end}
      </font></td></tr>" >> #{@file}`

      # update counting variables
      @cfuzzy   += values[1].to_i
      @cuntrans += values[2].to_i
      @cnotshow += notshow.to_i
      @clang    += 1
      CalcPercentage( per.to_i )

    end
  end

  puts "Entering Dir..."
  SrcDir()

  puts "Writing the header..."
  Header()

  puts "Writing the statistics..."
  langs = Dir.entries("po")
  Dir.foreach("po") {|lang|
    Stats( lang )
  }

  puts "Writing the footer..."
  Footer()

  FileUtils.mv( @file, ".." )
  puts "Creation finished..."
end
