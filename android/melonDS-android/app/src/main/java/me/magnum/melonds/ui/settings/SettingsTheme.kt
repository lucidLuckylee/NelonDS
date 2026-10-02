package me.magnum.melonds.ui.settings

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material.LocalContentColor
import androidx.compose.material.MaterialTheme
import androidx.compose.material.darkColors
import androidx.compose.material.lightColors
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.res.colorResource
import me.magnum.melonds.R

/** Compose settings share the preference screens' day/night palette. */
@Composable
fun SettingsTheme(content: @Composable () -> Unit) {
    val base = if (isSystemInDarkTheme()) darkColors() else lightColors()
    val colors = base.copy(
        primary = colorResource(R.color.settings_accent),
        secondary = colorResource(R.color.settings_accent),
        background = colorResource(R.color.settings_background),
        surface = colorResource(R.color.settings_surface),
        onSurface = colorResource(R.color.settings_text),
        onBackground = colorResource(R.color.settings_text),
    )
    MaterialTheme(colors = colors) {
        CompositionLocalProvider(LocalContentColor provides colors.onSurface, content = content)
    }
}
