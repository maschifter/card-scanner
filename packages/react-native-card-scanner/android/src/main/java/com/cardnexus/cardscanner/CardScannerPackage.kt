package com.cardnexus.cardscanner

import com.facebook.react.BaseReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.model.ReactModuleInfo
import com.facebook.react.module.model.ReactModuleInfoProvider
import com.facebook.react.uimanager.ViewManager

class CardScannerPackage : BaseReactPackage() {
  override fun createViewManagers(reactContext: ReactApplicationContext): List<ViewManager<*, *>> = listOf()

  override fun getModule(
    name: String,
    reactContext: ReactApplicationContext,
  ): NativeModule? =
    if (name == CardScannerInstaller.NAME) {
      CardScannerInstaller(reactContext)
    } else {
      null
    }

  override fun getReactModuleInfoProvider(): ReactModuleInfoProvider =
    ReactModuleInfoProvider {
      val moduleInfos: MutableMap<String, ReactModuleInfo> = HashMap()
      moduleInfos[CardScannerInstaller.NAME] =
        ReactModuleInfo(
          CardScannerInstaller.NAME,
          CardScannerInstaller.NAME,
          false, // canOverrideExistingModule
          false, // needsEagerInit
          false, // hasConstants
          false, // isCxxModule
          true
        )

      moduleInfos
    }
}
