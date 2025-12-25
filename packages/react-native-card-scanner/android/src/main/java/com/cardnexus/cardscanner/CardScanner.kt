package com.cardnexus.cardscanner

import com.facebook.jni.HybridData
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactMethod
import com.facebook.react.common.annotations.FrameworkAPI
import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.turbomodule.core.CallInvokerHolderImpl
import android.util.Log
import android.content.Context

@OptIn(FrameworkAPI::class)
@ReactModule(name = CardScannerInstaller.NAME)
class CardScannerInstaller(reactContext: ReactApplicationContext) :
  NativeCardScannerSpec(reactContext) {
  companion object {
    const val NAME = NativeCardScannerSpec.NAME
  }

  private val mHybridData: HybridData

  external fun initHybrid(jsContext: Long, callInvoker: CallInvokerHolderImpl): HybridData

  private external fun injectJSIBindings()

  external fun setDbPath(path: String)
  external fun setCachePath(path: String)

  init {
    try {
      System.loadLibrary("executorch")
      Log.d("CardScannerInstaller", "Loaded library executorch")
      System.loadLibrary("react-native-card-scanner")
      val jsCallInvokerHolder = reactContext.jsCallInvokerHolder as CallInvokerHolderImpl
      mHybridData = initHybrid(reactContext.javaScriptContextHolder!!.get(), jsCallInvokerHolder)
    } catch (exception: UnsatisfiedLinkError) {
      throw RuntimeException("Could not load native module CardScannerInstaller. Have you packaged the .so files correctly?", exception)
    }
  }

  @ReactMethod(isBlockingSynchronousMethod = true)
  override fun install(): Boolean {

    val databaseDir = reactApplicationContext.getDir("database", Context.MODE_PRIVATE)
    setDbPath(databaseDir.absolutePath)
    // Set cache path (temporary storage for images)
    val cacheDir = reactApplicationContext.cacheDir
    val imageCacheDir = java.io.File(cacheDir, "card-images")
    if (!imageCacheDir.exists()) {
      imageCacheDir.mkdirs()
    }
    setCachePath(imageCacheDir.absolutePath)

    Log.d("CardScannerInstaller", "Database path: ${databaseDir.absolutePath}")
    Log.d("CardScannerInstaller", "Cache path: ${imageCacheDir.absolutePath}")

    injectJSIBindings()
    return true
  }
}
