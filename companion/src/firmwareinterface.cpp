/*
 * Author - Bertrand Songis <bsongis@gmail.com>
 * 
 * Based on th9x -> http://code.google.com/p/th9x/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <QtGui>
#include "hexinterface.h"
#include "splash.h"
#include "firmwareinterface.h"
#include "helpers.h"

#define FW_MARK     "FW"
#define VERS_MARK   "VERS"
#define DATE_MARK   "DATE"
#define TIME_MARK   "TIME"
#define EEPR_MARK   "EEPR"

int getFileType(const QString &fullFileName)
{
  QString suffix = QFileInfo(fullFileName).suffix().toUpper();
  if (suffix == "HEX")
    return FILE_TYPE_HEX;
  else if (suffix == "BIN")
    return FILE_TYPE_BIN;
  else if (suffix == "EEPM")
    return FILE_TYPE_EEPM;
  else if (suffix == "EEPE")
    return FILE_TYPE_EEPE;
  else if (suffix == "XML")
    return FILE_TYPE_XML;
  else
    return 0;
}

FirmwareInterface::FirmwareInterface(const QString &filename):
  flash(MAX_FSIZE, 0),
  flashSize(0),
  versionId(0),
  eepromVersion(0),
  eepromVariant(0),
  splashOffset(0),
  splashSize(0),
  splashWidth(0),
  splashHeight(0),
  isValidFlag(false)
{
  if (!filename.isEmpty()) {
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) { // reading HEX TEXT file
      QTextStream inputStream(&file);
      flashSize = HexInterface(inputStream).load((uint8_t *)flash.data(), MAX_FSIZE);
      file.close();
      if (flashSize == 0) {
        file.open(QIODevice::ReadOnly);
        flashSize = file.read((char *)flash.data(), MAX_FSIZE);
      }
    }
  }

  if (flashSize > 0) {
    flavour = seekLabel(FW_MARK);
    version = seekLabel(VERS_MARK);
    if (version.startsWith("opentx-")) {
      // old version format
      int index = version.lastIndexOf('-');
      flavour = version.mid(0, index);
      version = version.mid(index+1);
    }
    date = seekLabel(DATE_MARK);
    time = seekLabel(TIME_MARK);
    eepromId = seekLabel(EEPR_MARK);

    if (eepromId.contains('-')) {
      QStringList list = eepromId.split('-');
      eepromVersion = list[0].toInt();
      eepromVariant = list[1].toInt();
    }
    else {
      eepromVersion = eepromId.toInt();
    }

    versionId = version2index(version);
    SeekSplash();
    isValidFlag = !version.isEmpty();
  }
}

QString FirmwareInterface::seekString(const QString & string)
{
  QString result = "";

  int start = flash.indexOf(string);
  if (start > 0) {
    start += string.length();
    int end = -1;
    for (int i=start; i<start+50; i++) {
      char c = flash.at(i);
      if (c == '\0' || c == '\036') {
        end = i;
        break;
      }
    }
    if (end > 0) {
      result = flash.mid(start, (end - start)).trimmed();
    }
  }

  return result;
}

QString FirmwareInterface::seekLabel(const QString & label)
{
  QString result = seekString(label + "\037\033:");
  if (!result.isEmpty())
    return result;

  return seekString(label + ":");
}

QString FirmwareInterface::getFlavour() const
{
  if (flavour == "opentx-x9dp")
    return "opentx-taranis-plus";
  else if (flavour == "opentx-x9d")
    return "opentx-taranis";
  else
    return flavour;
}

bool FirmwareInterface::isHardwareCompatible(const FirmwareInterface &previousFirmware) const
{
  QString newFlavour = getFlavour();
  if (newFlavour.isEmpty())
    return true;

  QString previousFlavour = previousFirmware.getFlavour();
  if (previousFlavour.isEmpty())
    return true;

  return (newFlavour == previousFlavour);
}

bool FirmwareInterface::SeekSplash(QByteArray splash)
{
  int start = flash.indexOf(splash);
  if (start>0) {
    splashOffset = start;
    splashSize = splash.size();
    return true;
  }
  else {
    return false;
  }
}

bool FirmwareInterface::SeekSplash(QByteArray sps, QByteArray spe, int size)
{
  int start = 0;
  while (start>=0) {
    start = flash.indexOf(sps, start+1);
    if (start>0) {
      int end = start + sps.size() + size;
      if (end == flash.indexOf(spe, end)) {
        splashOffset = start + sps.size();
        splashSize = end - start - sps.size();
        return true;
      }
      else {
        qDebug() << flash.indexOf(spe, start) << end << sps.size() << spe;
      }
    }
  }
  return false;
}

#define OTX_SPS_9X      "SPS\0\200\100"
#define OTX_SPS_TARANIS "SPS\0\324\100"
#define OTX_SPS_SIZE    6
#define OTX_SPE         "SPE"
#define OTX_SPE_SIZE    4

void FirmwareInterface::SeekSplash(void) 
{
  splashSize = 0;
  splashOffset = 0;
  splashWidth = SPLASH_WIDTH;
  splashHeight = SPLASH_HEIGHT;
  splash_format = QImage::Format_Mono;

  if (SeekSplash(QByteArray((const char *)gr9x_splash, sizeof(gr9x_splash))) || SeekSplash(QByteArray((const char *)gr9xv4_splash, sizeof(gr9xv4_splash)))) {
    return;
  }

  if (SeekSplash(QByteArray((const char *)er9x_splash, sizeof(er9x_splash)))) {
    return;
  }

  if (SeekSplash(QByteArray((const char *)opentx_splash, sizeof(opentx_splash)))) {
    return;
  }

  if (SeekSplash(QByteArray((const char *)opentxtaranis_splash, sizeof(opentxtaranis_splash)))) {
    splashWidth = SPLASHX9D_WIDTH;
    splashHeight = SPLASHX9D_HEIGHT;
    splash_format = QImage::Format_Indexed8;
    return;
  }

  if (SeekSplash(QByteArray((const char *)ersky9x_splash, sizeof(ersky9x_splash)))) {
    return;
  }

  if (SeekSplash(QByteArray(OTX_SPS_9X, OTX_SPS_SIZE), QByteArray(OTX_SPE, OTX_SPE_SIZE), 1024)) {
    return;
  }

  if (SeekSplash(QByteArray(OTX_SPS_TARANIS, OTX_SPS_SIZE), QByteArray(OTX_SPE, OTX_SPE_SIZE), 6784)) {
    splashWidth = SPLASHX9D_WIDTH;
    splashHeight = SPLASHX9D_HEIGHT;
    splash_format = QImage::Format_Indexed8;
    return;
  }

  if (SeekSplash(QByteArray(ERSKY9X_SPS, sizeof(ERSKY9X_SPS)), QByteArray(ERSKY9X_SPE, sizeof(ERSKY9X_SPE)), 1030)) {
    return;
  }

  if (SeekSplash(QByteArray(ERSPLASH_MARKER, sizeof(ERSPLASH_MARKER)))) {
    splashOffset += sizeof(ERSPLASH_MARKER);
    splashSize = sizeof(er9x_splash);
  }
}

bool FirmwareInterface::setSplash(const QImage & newsplash)
{
  if (splashOffset == 0 || splashSize == 0) {
    return false;
  }

  char b[SPLASH_SIZE_MAX] = {0};
  QColor color;
  QByteArray splash;
  if (splash_format == QImage::Format_Indexed8) {
    for (unsigned int y=0; y<splashHeight; y++) {
      unsigned int idx = (y/2)*splashWidth;
      for (unsigned int x=0; x<splashWidth; x++, idx++) {
        QRgb gray = qGray(newsplash.pixel(x, y));
        uint8_t z = ((255-gray)*15)/255;
        if (y & 1) z <<= 4;
        b[idx] |= z;
      }
    }
  }
  else {
    QColor black = QColor(0,0,0);
    QImage blackNwhite = newsplash.convertToFormat(QImage::Format_MonoLSB);
    for (uint y=0; y<splashHeight; y++) {
      for (uint x=0; x<splashWidth; x++) {
        color = QColor(blackNwhite.pixel(x,y));
        b[splashWidth*(y/8) + x] |= ((color==black ? 1: 0)<<(y % 8));
      }
    }
  }
  splash.clear();
  splash.append(b, splashSize);
  flash.replace(splashOffset, splashSize, splash);
  return true;
}

int FirmwareInterface::getSplashWidth()
{
  return splashWidth;
}

uint FirmwareInterface::getSplashHeight()
{
  return splashHeight;
}

QImage::Format FirmwareInterface::getSplashFormat()
{
  return splash_format;
}

QImage FirmwareInterface::getSplash()
{
  if (splashOffset == 0 || splashSize == 0) {
    return QImage(); // empty image
  }

  if (splash_format == QImage::Format_Indexed8) {
    QImage image(splashWidth, splashHeight, QImage::Format_RGB888);
    if (splashOffset > 0) {
      for (unsigned int y=0; y<splashHeight; y++) {
        unsigned int idx = (y/2)*splashWidth;
        for (unsigned int x=0; x<splashWidth; x++, idx++) {
          uint8_t byte = flash.at(splashOffset+idx);
          unsigned int z = (y & 1) ? (byte >> 4) : (byte & 0x0F);
          z = 255-(z*255)/15;
          QRgb rgb = qRgb(z, z, z);
          image.setPixel(x, y, rgb);
        }
      }
    }
    return image;
  }
  else {
    QImage image(splashWidth, splashHeight, QImage::Format_Mono);
    if (splashOffset > 0) {
      for (unsigned int y=0; y<splashHeight; y++) {
        for(unsigned int x=0; x<splashWidth; x++) {
          image.setPixel(x, y, (flash.at(splashOffset+(splashWidth*(y/8)+x)) & (1<<(y % 8))) ? 0 : 1);
        }
      }
    }
    return image;
  }
}

bool FirmwareInterface::hasSplash()
{
  return (splashOffset > 0 ? true : false);
}

bool FirmwareInterface::isValid()
{
  return isValidFlag;
}

unsigned int FirmwareInterface::save(QString fileName)
{
  uint8_t binflash[MAX_FSIZE];
  memcpy(&binflash, flash.constData(), flashSize);
  QFile file(fileName);
  
  int fileType = getFileType(fileName);

  if (fileType == FILE_TYPE_HEX) {
    if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) { //reading HEX TEXT file
      return -1;
    }
    QTextStream outputStream(&file);
    HexInterface hex=HexInterface(outputStream);
    hex.save(binflash, flashSize);
  }
  else {
    if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate)) { //reading HEX TEXT file
      return -1;
    }
    file.write((char*)binflash, flashSize);
  }

  file.close();

  return flashSize;
}
